/*
 * GlideMix
 */
#include "mixhousephrasemodel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include "mixhouseanalyzer.h"
#include "log.h"

#ifdef MIXHOUSE_HAVE_BEAT_THIS
#include <onnxruntime_cxx_api.h>
#endif

using namespace au::mixhouse;

namespace {
constexpr int kModelBeatsPerBar = 4;
constexpr int kModelRoles = 5;   // Intro, Up/Verse, Down/Bridge, Chorus, Outro
constexpr int kModelMinBars = 16;

//! The loudness detector's names, as the model's roles.
int modelBuiltInRole(const QString& label)
{
    const QString word = label.section(u' ', 0, 0);
    if (word == u"Intro") {
        return 0;
    }
    if (word == u"Up") {
        return 1;
    }
    if (word == u"Down") {
        return 2;
    }
    if (word == u"Chorus") {
        return 3;
    }
    if (word == u"Outro") {
        return 4;
    }
    return -1;
}
}

PhraseModelInput au::mixhouse::phraseModelInput(const std::vector<float>& energy, double bpm, double firstBeatSec,
                                                double downbeatSec, const std::vector<MixHousePhrase>& builtIn)
{
    PhraseModelInput in;
    const int beats = static_cast<int>(energy.size() / 3);
    if (bpm <= 0.0 || beats < 64) {
        return in;
    }
    // Exactly as the trainer builds them (p2_app.py bar_features).
    const double period = 60.0 / bpm;
    const double t0 = firstBeatSec - std::floor(firstBeatSec / period) * period;
    const int phase = ((static_cast<int>(std::lround((downbeatSec - t0) / period)) % kModelBeatsPerBar) + kModelBeatsPerBar)
                      % kModelBeatsPerBar;
    std::vector<int> starts;
    for (int s = phase; s < beats - 3; s += kModelBeatsPerBar) {
        starts.push_back(s);
    }
    const int n = static_cast<int>(starts.size());
    in.barSec = kModelBeatsPerBar * period;
    for (int i = 0; i < n; ++i) {
        const int s = starts[i];
        const double tm = t0 + s * period;
        float mean[3] = {};
        for (int b = 0; b < kModelBeatsPerBar; ++b) {
            for (int k = 0; k < 3; ++k) {
                const float v = energy[(s + b) * 3 + k];
                in.rows.push_back(v);
                mean[k] += v / kModelBeatsPerBar;
            }
        }
        in.rows.insert(in.rows.end(), mean, mean + 3);
        float today[kModelRoles] = {};
        const double mid = tm + 2.0 * period;
        for (const MixHousePhrase& p : builtIn) {
            if (p.startSec <= mid && mid < p.endSec) {
                const int r = modelBuiltInRole(p.label);
                if (r >= 0) {
                    today[r] = 1.0f;
                }
                break;
            }
        }
        in.rows.insert(in.rows.end(), today, today + kModelRoles);
        const float pos = static_cast<float>(i) / std::max(1, n - 1);
        in.rows.push_back(pos);
        in.rows.push_back(1.0f - pos);
        in.rows.push_back(static_cast<float>((bpm - 98.0) / 98.0));
        in.barStarts.push_back(tm);
    }
    return in;
}

#ifdef MIXHOUSE_HAVE_BEAT_THIS
namespace {
//! The model, loaded once; null if the file is missing or broken.
Ort::Session* phraseSession()
{
    static std::once_flag once;
    static std::unique_ptr<Ort::Env> env;
    static std::unique_ptr<Ort::Session> session;
    std::call_once(once, [] {
        const QString path = mixHouseModelPath(QStringLiteral("glidemix_phrases.onnx"),
                                               QStringLiteral(MIXHOUSE_PHRASE_MODEL_DEV_PATH));
        if (path.isEmpty()) {
            LOGI() << "GlideMix Phrase Detector not installed -- phrases from the loudness detector";
            return;
        }
        try {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "glidemix-phrases");
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(1);
#ifdef _WIN32
            const std::wstring file = path.toStdWString();   // ORTCHAR_T is wchar_t on Windows
#else
            const std::string file = path.toStdString();
#endif
            session = std::make_unique<Ort::Session>(*env, file.c_str(), options);
            LOGI() << "GlideMix Phrase Detector loaded from " << path;
        } catch (const std::exception& e) {
            LOGW() << "GlideMix Phrase Detector could not be loaded: " << e.what();
            session.reset();
        }
    });
    return session.get();
}

QString modelRoleName(int role, int mood)
{
    // rekordbox's words: high mood (EDM) Intro/Up/Down/Chorus/Outro; mid and
    // low mood Intro/Verse/Bridge/Chorus/Outro.
    static const char* high[] = { "Intro", "Up", "Down", "Chorus", "Outro" };
    static const char* other[] = { "Intro", "Verse", "Bridge", "Chorus", "Outro" };
    return QString::fromLatin1(mood == 0 ? high[role] : other[role]);
}
}
#endif

std::vector<MixHousePhrase> au::mixhouse::modelPhrases(const std::vector<float>& energy, double bpm,
                                                       double firstBeatSec, double downbeatSec, double durationSec,
                                                       const std::vector<MixHousePhrase>& builtIn)
{
#ifdef MIXHOUSE_HAVE_BEAT_THIS
    Ort::Session* session = phraseSession();
    if (!session) {
        return {};
    }
    PhraseModelInput in = phraseModelInput(energy, bpm, firstBeatSec, downbeatSec, builtIn);
    const int n = static_cast<int>(in.barStarts.size());
    if (n < kModelMinBars) {
        return {};
    }
    std::vector<float> roleLogits, boundary, mood;
    try {
        const int64_t shape[3] = { 1, n, kPhraseModelFeatures };
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, in.rows.data(), in.rows.size(), shape, 3);
        const char* inputNames[] = { "bars" };
        const char* outputNames[] = { "role", "boundary", "mood" };
        std::vector<Ort::Value> out = session->Run(Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 3);
        const float* r = out[0].GetTensorData<float>();
        const float* b = out[1].GetTensorData<float>();
        const float* m = out[2].GetTensorData<float>();
        roleLogits.assign(r, r + n * kModelRoles);
        boundary.assign(b, b + n);
        mood.assign(m, m + 3);
    } catch (const std::exception& e) {
        LOGW() << "GlideMix Phrase Detector failed: " << e.what();
        return {};
    }

    // Phrase starts: the boundary peaks above even odds; then one name per
    // phrase, from the summed role probabilities (as the trainer scores it).
    std::vector<int> cuts { 0 };
    for (int i = 1; i < n; ++i) {
        const float p = 1.0f / (1.0f + std::exp(-boundary[i]));
        const bool peak = boundary[i] >= boundary[i - 1] && (i + 1 >= n || boundary[i] >= boundary[i + 1]);
        if (p > 0.5f && peak) {
            cuts.push_back(i);
        }
    }
    cuts.push_back(n);
    const int moodIndex = static_cast<int>(std::max_element(mood.begin(), mood.end()) - mood.begin());

    std::vector<MixHousePhrase> phrases;
    int counts[kModelRoles] = {};
    for (size_t c = 0; c + 1 < cuts.size(); ++c) {
        double sums[kModelRoles] = {};
        for (int i = cuts[c]; i < cuts[c + 1]; ++i) {
            const float* l = &roleLogits[i * kModelRoles];
            const float top = *std::max_element(l, l + kModelRoles);
            double total = 0.0, e[kModelRoles];
            for (int k = 0; k < kModelRoles; ++k) {
                e[k] = std::exp(l[k] - top);
                total += e[k];
            }
            for (int k = 0; k < kModelRoles; ++k) {
                sums[k] += e[k] / total;
            }
        }
        const int role = static_cast<int>(std::max_element(sums, sums + kModelRoles) - sums);
        const double start = c == 0 ? 0.0 : in.barStarts[cuts[c]];
        const double end = cuts[c + 1] < n ? in.barStarts[cuts[c + 1]] : durationSec;
        // A phrase start keeps its phrase even when the name repeats: rekordbox
        // also has Up 1 straight into Up 2, or Chorus after Chorus.
        // Numbered as rekordbox numbers them: high mood Intro 1, Up 1-3,
        // Chorus 1-2, Outro 1 (Down plain); other moods only Verse 1-2.
        const bool high = moodIndex == 0;
        const int maxNumber = high ? (role == 1 ? 3 : role == 3 ? 2 : role == 2 ? 0 : 1) : (role == 1 ? 2 : 0);
        const int number = std::min(++counts[role], maxNumber);
        MixHousePhrase phrase;
        phrase.startSec = start;
        phrase.endSec = end;
        phrase.label = maxNumber > 0 ? QStringLiteral("%1 %2").arg(modelRoleName(role, moodIndex)).arg(number)
                                     : modelRoleName(role, moodIndex);
        phrase.color = mixHousePhraseColor(phrase.label);
        phrases.push_back(phrase);
    }
    return phrases;
#else
    Q_UNUSED(energy);
    Q_UNUSED(bpm);
    Q_UNUSED(firstBeatSec);
    Q_UNUSED(downbeatSec);
    Q_UNUSED(durationSec);
    Q_UNUSED(builtIn);
    return {};
#endif
}
