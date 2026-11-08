# GlideMix v1.1.31

## Fixed

- **Windows: freezes with network drives.** If a mapped network drive was offline or not ready (its computer switched off, a share not available), GlideMix froze for up to 40 seconds, about every 40 seconds, even when you were not doing anything. GlideMix now looks for DJ libraries only on local drives (internal, USB, SD card) and does it in the background, so a slow drive can no longer hold up the window.
- A DJ app library kept on a network share is still read: add its folder in Preferences > Library > Other DJ Apps.

Thanks to the tester who sent a very precise report.
