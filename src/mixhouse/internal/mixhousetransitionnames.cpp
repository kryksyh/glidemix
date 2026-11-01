/*
 * GlideMix
 */
#include "mixhousetransitionnames.h"

#include "au3-registries/ClientData.h"
#include "au3-project/Project.h"
#include "au3-xml/XMLAttributeValueView.h"
#include "au3-xml/XMLWriter.h"

namespace {
struct TransitionNames : public ClientData::Base
{
    std::function<std::string()> writer;
    std::string loaded;

    static TransitionNames& Get(AudacityProject& project);
};

const AttachedProjectObjects::RegisteredFactory key {
    [](AudacityProject&) { return std::make_shared<TransitionNames>(); }
};

TransitionNames& TransitionNames::Get(AudacityProject& project)
{
    return project.AttachedObjects::Get<TransitionNames&>(key);
}

ProjectFileIORegistry::AttributeWriterEntry entry {
    [](const AudacityProject& project, XMLWriter& xmlFile) {
        auto& names = TransitionNames::Get(const_cast<AudacityProject&>(project));
        const std::string value = names.writer ? names.writer() : names.loaded;
        if (!value.empty()) {
            xmlFile.WriteAttr(wxT("glidemix_transitions"), wxString::FromUTF8(value));
        }
    }
};

ProjectFileIORegistry::AttributeReaderEntries entries {
    (TransitionNames & (*)(AudacityProject&)) & TransitionNames::Get, {
        { "glidemix_transitions", [](auto& names, auto value) {
                names.loaded = value.ToWString().ToStdString(wxConvUTF8);
            } }
    }
};
}

void au::mixhouse::setTransitionNamesWriter(void* au3Project, std::function<std::string()> writer)
{
    if (au3Project) {
        TransitionNames::Get(*reinterpret_cast<AudacityProject*>(au3Project)).writer = std::move(writer);
    }
}

std::string au::mixhouse::loadedTransitionNames(void* au3Project)
{
    return au3Project ? TransitionNames::Get(*reinterpret_cast<AudacityProject*>(au3Project)).loaded : std::string();
}
