#ifndef EVO_WEB_UPLOAD_SERVICE_HPP
#define EVO_WEB_UPLOAD_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace evo {

enum class WebUploadStatus {
    Starting,
    WaitingForStorage,
    WaitingForAddress,
    ListenerError,
    Ready,
};

struct WebUploadInfo {
    std::string address;
    WebUploadStatus status = WebUploadStatus::Starting;
};

void applyWebUploadSetting(bool enabled);
void requestWebUploadStop();
void stopWebUpload();
WebUploadInfo getWebUploadInfo();
uint64_t webUploadRevision();
std::vector<std::string> takeWebDeletedVideos();

} // namespace evo
#endif
