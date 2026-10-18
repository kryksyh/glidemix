/*
 * GlideMix
 */
#include "MicrophoneAccess.h"

#import <AVFoundation/AVFoundation.h>

bool MicrophoneAccessGranted()
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized;
}
