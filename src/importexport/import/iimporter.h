#pragma once

#include "io/path.h"
#include "modularity/imoduleinterface.h"

#include "types/importtypes.h"
#include "trackedit/trackedittypes.h"

namespace au::importexport {
class IImporter : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IImporter)

public:
    virtual ~IImporter() = default;

    virtual FileInfo fileInfo(const muse::io::path_t& filePath) = 0;
    //! detectTempo: see importIntoTrack.
    virtual bool import(const muse::io::path_t& filePath, bool detectTempo = true) = 0;
    //! detectTempo: run the import tempo detection (and its dialogs). Off for
    //! files whose tempo is already known -- a library track -- where the
    //! detection's guess would be overwritten anyway.
    virtual bool importIntoTrack(const muse::io::path_t& filePath, trackedit::TrackId dstTrackId, trackedit::secs_t startTime,
                                 bool detectTempo = true) = 0;
    virtual bool importFromSystemClipboard(const std::vector<muse::io::path_t>& filePaths, trackedit::secs_t startTime) = 0;
    virtual std::vector<std::string> supportedExtensions() const = 0;
};
}
