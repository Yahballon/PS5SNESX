// PS5SX2 (vk-285-73): PS5 notifications, sent from a worker thread so a caller (the pad thread, the
// CPU thread) never waits on the system. See ProsperoNotify.cpp.
#pragma once

// The kernel toast (sceKernelSendNotificationRequest): text only, what the port has used since the
// start ("PS5SX2: starting", "Now playing: ...").
void OrbisNotifyPlain(const char* text);

// A rich toast (libSceNotification's sceNotificationSend with a JSON payload): a message, a second
// line and an icon, given as a file path the system UI can read or a URL. Falls back to the kernel
// toast when the library or the call fails. Needs proper testing: the layout is the public SDK
// sample's, and which icon sources the system UI accepts is not known yet.
void OrbisNotifyRich(const char* message, const char* sub_message, const char* icon);
