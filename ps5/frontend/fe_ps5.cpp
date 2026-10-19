// PS5 port frontend: the console side. The shelf runs on its own Vulkan device (the driver linked
// into the eboot) with a VK_KHR_display swapchain on VideoOut, reads the DualSense, plays its key
// sounds on an audio port of its own, and downloads missing covers over HTTPS with the console's
// libcurl/OpenSSL when configured, otherwise libSceHttp2. Everything is torn down again before
// PCSX2 opens its device, VideoOut and audio.
//
// Copyright (C) 2026 Spyros
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_ps5.h"
#include "shelf_runtime.h"
#include "loading.h"
#include "fe_nfs.h"
#include "fe_file_io.h"

#include "fe_app.h"
#include "fe_covers.h"
#include "fe_exact_porpoise.h"
#include "cover_urls.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_renderer.h"
#include "fe_sound.h"
#include "fe_text.h"
#include "fe_vk.h"
#include "fe_web.h"
#include "third_party/qrcodegen/qrcodegen.h" // vk-285-113: orbis_web_qr

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib> // ::exit after releasing Vulkan and the controller
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#ifdef ORBIS_CURL
#include <curl/curl.h>
#endif

// What sceKernelConvertUtcToLocaltime fills in: 16 bytes (as the open-source KytyPS5 emulator
// implements it), not a struct timezone. vk-285-40 passed an 8-byte struct timezone and an int for
// the DST offset; the call wrote the zone offsets (7200, 3600) over the next stack slot, which held
// the swapchain pointer, and the first vkQueuePresentKHR faulted on it.
struct KernelTimesec
{
	int64_t t;
	uint32_t west_sec; // seconds east of UTC, despite the name
	uint32_t dst_sec;
};

extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, void* data);
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceKernelConvertUtcToLocaltime(time_t utc, time_t* local, KernelTimesec* sec, uint64_t* dst_sec);
int sceSystemServiceParamGetInt(int param, int* value);
int sceSystemServiceHideSplashScreen(void);

// libSceNet, libSceSsl and libSceHttp2 as the payload SDK's http2_get sample declares them. The
// system loads all three into every app already, so linking them adds nothing at start-up.
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetPoolDestroy(int pool);
int sceSslInit(size_t pool_size);
int sceSslTerm(int ctx);
int sceHttp2Init(int net_pool, int ssl_ctx, size_t pool_size, int max_requests);
int sceHttp2Term(int ctx);
int sceHttp2CreateTemplate(int ctx, const char* user_agent, int http_version, int auto_proxy);
int sceHttp2DeleteTemplate(int tmpl);
int sceHttp2CreateRequestWithURL(int tmpl, const char* method, const char* url, uint64_t content_length);
int sceHttp2DeleteRequest(int req);
int sceHttp2SendRequest(int req, const void* data, size_t size);
int sceHttp2GetStatusCode(int req, int* status);
int sceHttp2ReadData(int req, void* data, size_t size);
int sceHttp2SetResolveTimeOut(int id, uint32_t usec);
int sceHttp2SetConnectTimeOut(int id, uint32_t usec);
int sceHttp2SetSendTimeOut(int id, uint32_t usec);
int sceHttp2SetRecvTimeOut(int id, uint32_t usec);
int sceHttp2AbortRequest(int req);
int sceHttp2SetTimeOut(int id, uint32_t usec);
int sceHttp2SetAutoRedirect(int id, int enable);
int sceNetCtlInit(void);
void sceNetCtlTerm(void);
int sceNetCtlGetState(int* state);

// libSceAudioOut, as orbis-shims/ProsperoAudio.cpp declares it for PCSX2's own output.
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param);
int sceAudioOutOutput(int handle, const void* p);
int sceAudioOutClose(int handle);
}

// The fonts, embedded: PCSX2's Roboto (Apache-2.0) and PromptFont (OFL-1.1), from its resources.
#ifndef FE_FONT_DIR
#error "FE_FONT_DIR must name PCSX2's bin/resources/fonts"
#endif
#define FE_INCBIN(sym, file) \
	__asm__(".section .rodata." #sym ",\"a\",@progbits\n" \
			".balign 16\n" \
			".global " #sym "\n" #sym ":\n" \
			".incbin \"" FE_FONT_DIR "/" file "\"\n" \
			".global " #sym "_end\n" #sym "_end:\n" \
			".byte 0\n" \
			".previous\n")
FE_INCBIN(fe_font_text, "Roboto-Regular.ttf");
FE_INCBIN(fe_font_icons, "promptfont.otf");
extern "C" const uint8_t fe_font_text[], fe_font_text_end[], fe_font_icons[], fe_font_icons_end[];

// vk-285-50: the frontend's own files (frontend/assets): Font Awesome Free's brands font (SIL OFL
// 1.1) for the handles' icons, and the settings page with its two icons (Font Awesome, CC BY 4.0).
#ifndef FE_ASSET_DIR
#error "FE_ASSET_DIR must name frontend/assets"
#endif
#define FE_INCBIN_ASSET(sym, file) \
	__asm__(".section .rodata." #sym ",\"a\",@progbits\n" \
			".balign 16\n" \
			".global " #sym "\n" #sym ":\n" \
			".incbin \"" FE_ASSET_DIR "/" file "\"\n" \
			".global " #sym "_end\n" #sym "_end:\n" \
			".byte 0\n" \
			".previous\n")
FE_INCBIN_ASSET(fe_font_brands, "fonts/fa-brands-400.otf");
#ifdef ORBIS_CURL
FE_INCBIN_ASSET(fe_cover_ca, "ca-bundle.crt");
extern "C" const uint8_t fe_cover_ca[], fe_cover_ca_end[];
#endif
FE_INCBIN_ASSET(fe_web_page, "web/index.html");
FE_INCBIN_ASSET(fe_web_discord, "web/discord.svg");
FE_INCBIN_ASSET(fe_web_x, "web/x-twitter.svg");
// vk-285-51: the recommended settings the page's Recommended button restores.
FE_INCBIN_ASSET(fe_presets, "presets.ini");
// Project Porpoise original menu effects, copyright Ruben (@elripalda),
// GPL-3.0-or-later; fetched from a pinned upstream commit during CI.
FE_INCBIN_ASSET(fe_porpoise_game_row, "sounds/game-row.wav");
FE_INCBIN_ASSET(fe_porpoise_menu_scroll, "sounds/menu-scroll.wav");
// Original Porpoise font and icon files shipped inside our eboot, so the
// default /app0 mount path is NOT a runtime dependency on PS5 homebrew.
FE_INCBIN_ASSET(fe_pp_nunito_regular, "porpoise/fonts/Nunito-Regular.ttf");
FE_INCBIN_ASSET(fe_pp_nunito_semibold, "porpoise/fonts/Nunito-SemiBold.ttf");
FE_INCBIN_ASSET(fe_pp_nunito_bold, "porpoise/fonts/Nunito-Bold.ttf");
FE_INCBIN_ASSET(fe_pp_nunito_extrabold, "porpoise/fonts/Nunito-ExtraBold.ttf");
FE_INCBIN_ASSET(fe_pp_noto_jp, "porpoise/fonts/NotoSansJP-Porpoise.ttf");
// The built-in tiger replaces the original Dolphin brand mask.
FE_INCBIN_ASSET(fe_pp_dolphin_mask, "snesxy-mark.png");
FE_INCBIN_ASSET(fe_pp_buttons, "porpoise/ui/buttons.png");
// A original 1024x1536 user-supplied NO COVER PNG is baked into eboot.bin (works offline).
FE_INCBIN_ASSET(fe_snesxy_default, "snesxy-mark.png");
extern "C" const uint8_t fe_pp_nunito_regular[], fe_pp_nunito_regular_end[],
    fe_pp_nunito_semibold[], fe_pp_nunito_semibold_end[],
    fe_pp_nunito_bold[], fe_pp_nunito_bold_end[],
    fe_pp_nunito_extrabold[], fe_pp_nunito_extrabold_end[],
    fe_pp_noto_jp[], fe_pp_noto_jp_end[],
    fe_pp_dolphin_mask[], fe_pp_dolphin_mask_end[],
    fe_pp_buttons[], fe_pp_buttons_end[];

extern "C" const uint8_t fe_font_brands[], fe_font_brands_end[], fe_web_page[], fe_web_page_end[], fe_web_discord[],
	fe_web_discord_end[], fe_web_x[], fe_web_x_end[], fe_presets[], fe_presets_end[];

namespace
{
using namespace fe;

// xlenore/ps2-covers, the default set: <serial>.jpg, 512x736 (the same source as the user's Twiso).
constexpr const char* kCoverUrl = "https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/${title}.png";


struct EmbeddedPorpoiseAsset { const char* name; const uint8_t* begin; const uint8_t* end; };
bool PreparePorpoiseAssets(const std::string& root)
{
    const EmbeddedPorpoiseAsset assets[] = {
        {"fonts/Nunito-Regular.ttf", fe_pp_nunito_regular, fe_pp_nunito_regular_end},
        {"fonts/Nunito-SemiBold.ttf", fe_pp_nunito_semibold, fe_pp_nunito_semibold_end},
        {"fonts/Nunito-Bold.ttf", fe_pp_nunito_bold, fe_pp_nunito_bold_end},
        {"fonts/Nunito-ExtraBold.ttf", fe_pp_nunito_extrabold, fe_pp_nunito_extrabold_end},
        {"fonts/NotoSansJP-Porpoise.ttf", fe_pp_noto_jp, fe_pp_noto_jp_end},
        {"brand/dolphin-mask.png", fe_pp_dolphin_mask, fe_pp_dolphin_mask_end},
        {"ui/buttons.png", fe_pp_buttons, fe_pp_buttons_end},
    };
    for (const std::string& dir : {root, root + "/fonts", root + "/brand", root + "/ui"})
    {
        if (mkdir(dir.c_str(), 0777) && errno != EEXIST)
        {
            std::printf("[Porpoise Exact] mkdir(%s): errno %d\n", dir.c_str(), errno);
            return false;
        }
    }
    for (const auto& a : assets)
    {
        const std::string name = root + "/" + a.name;
        const size_t bytes = static_cast<size_t>(a.end - a.begin);
        struct stat st{};
        if (stat(name.c_str(), &st) == 0 && static_cast<size_t>(st.st_size) == bytes)
            continue;
        const std::string tmp = name + ".new";
        int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
        {
            std::printf("[Porpoise Exact] open(%s): errno %d\n", tmp.c_str(), errno);
            return false;
        }
        size_t done = 0;
        bool complete = true;
        while (done < bytes)
        {
            const ssize_t n = write(fd, a.begin + done, bytes - done);
            if (n <= 0) { complete = false; break; }
            done += static_cast<size_t>(n);
        }
        if (close(fd) || !complete || rename(tmp.c_str(), name.c_str()))
        {
            unlink(tmp.c_str());
            std::printf("[Porpoise Exact] asset write failed: %s\n", name.c_str());
            return false;
        }
    }
    std::printf("[Porpoise Exact] original embedded UI assets are ready at %s\n", root.c_str());
    return true;
}

// The key sounds' output: a port on the main output, float stereo at 48 kHz in 256-frame grains,
// opened the way PCSX2's own output opens it once the game runs (system user, main port, format 4),
// and a thread that mixes whatever is playing into each grain. sceAudioOutOutput blocks until the
// previous grain has been consumed, which paces the thread.
class AudioOut
{
public:
	bool Start(Mixer* mixer)
	{
		const int init = sceAudioOutInit(); // PCSX2's output calls it again later and ignores the error
		m_handle = sceAudioOutOpen(255, 0, 0, kGrain, SoundBank::kRate, 4);
		std::printf("[frontend] sound: sceAudioOutInit %x, port %x\n", static_cast<unsigned>(init), static_cast<unsigned>(m_handle));
		if (m_handle < 0)
			return false;
		m_quit.store(false);
		m_mixer = mixer;
		m_thread = std::thread([this]() { Run(); });
		return true;
	}

	void Stop()
	{
		m_quit.store(true);
		if (m_thread.joinable())
			m_thread.join();
		if (m_handle >= 0)
			sceAudioOutClose(m_handle);
		m_handle = -1;
	}

private:
	static constexpr unsigned kGrain = 256;

	void Run()
	{
		alignas(64) float buf[kGrain * 2];
		while (!m_quit.load(std::memory_order_relaxed))
		{
			m_mixer->Mix(buf, static_cast<int>(kGrain));
			sceAudioOutOutput(m_handle, buf);
		}
	}

	Mixer* m_mixer = nullptr;
	int m_handle = -1;
	std::atomic<bool> m_quit{false};
	std::thread m_thread;
};

// libScePad's state, as game_select.cpp and main-boot.cpp read it.
struct PadData
{
	uint32_t buttons;
	uint8_t lx, ly, rx, ry, l2, r2, pad0, pad1;
	uint8_t rest[256];
};
constexpr uint32_t kPadRight = 0x20, kPadLeft = 0x80, kPadCross = 0x4000, kPadOptions = 0x8, kPadL1 = 0x400,
				   kPadR1 = 0x800;
// vk-285-114: the options sheet's buttons.
constexpr uint32_t kPadUp = 0x10, kPadDown = 0x40, kPadTriangle = 0x1000, kPadCircle = 0x2000, kPadSquare = 0x8000;
constexpr uint32_t kPadL2 = 0x100, kPadR2 = 0x200; // vk-285-116: the sheet's tabs

double Now()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<double>(ts.tv_sec) + ts.tv_nsec * 1e-9;
}

// The time of day in the console's time zone and clock format.
std::string Clock()
{
	const time_t utc = time(nullptr);
	time_t local = utc;
	// Room to spare after both outputs, so a firmware that writes more cannot reach our locals.
	KernelTimesec sec[2] = {};
	uint64_t dst[2] = {};
	if (sceKernelConvertUtcToLocaltime(utc, &local, &sec[0], &dst[0]) != 0)
		local = utc;
	struct tm tm = {};
	gmtime_r(&local, &tm);
	static int s_format = -1;
	if (s_format < 0)
	{
		int v = 1;
		s_format = sceSystemServiceParamGetInt(3 /* time format */, &v) == 0 ? v : 1;
	}
	char buf[16];
	if (s_format == 0) // 12-hour
		std::snprintf(buf, sizeof(buf), "%d:%02d %s", (tm.tm_hour + 11) % 12 + 1, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
	else
		std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
	return buf;
}

// ---- HTTPS on the cover worker's thread only ----
struct Http
{
#ifdef ORBIS_CURL
	bool curl_ready = false;
	struct Transfer
	{
		Http* owner;
		std::vector<uint8_t>* bytes;
	};
	static size_t Receive(char* data, size_t size, size_t count, void* opaque)
	{
		auto& transfer = *static_cast<Transfer*>(opaque);
		constexpr size_t limit = 16u << 20;
		if (size != 0 && count > limit / size)
			return 0;
		const size_t length = size * count;
		if (transfer.owner->stopping || transfer.bytes->size() > limit - length)
			return 0;
		transfer.bytes->insert(transfer.bytes->end(), data, data + length);
		return length;
	}
	static int Progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
	{
		return static_cast<Http*>(opaque)->stopping ? 1 : 0;
	}
	int GetCurl(const std::string& url, std::vector<uint8_t>& out)
	{
		last_error.clear();
		out.clear();
		if (stopping)
			return -1;
		if (!curl_ready)
		{
			sceNetInit();
			const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
			if (initialized != CURLE_OK)
			{
				Fail("curl init", initialized);
				return -1;
			}
			curl_ready = true;
			std::printf("[frontend] cover HTTPS: %s, embedded CA verification enabled\n", curl_version());
		}
		CURL* handle = curl_easy_init();
		if (!handle)
		{
			Fail("curl handle", CURLE_FAILED_INIT);
			return -1;
		}
		Transfer transfer{this, &out};
		char detail[CURL_ERROR_SIZE] = {};
		curl_blob ca{const_cast<uint8_t*>(fe_cover_ca), static_cast<size_t>(fe_cover_ca_end - fe_cover_ca), CURL_BLOB_COPY};
		CURLcode configured = CURLE_OK;
		auto option = [&](CURLoption key, auto value) {
			if (configured == CURLE_OK)
				configured = curl_easy_setopt(handle, key, value);
		};
		option(CURLOPT_URL, url.c_str());
		option(CURLOPT_PROTOCOLS_STR, "https");
		option(CURLOPT_REDIR_PROTOCOLS_STR, "https");
		option(CURLOPT_FOLLOWLOCATION, 1L);
		option(CURLOPT_MAXREDIRS, 5L);
		option(CURLOPT_SSL_VERIFYPEER, 1L);
		option(CURLOPT_SSL_VERIFYHOST, 2L);
		option(CURLOPT_CAINFO_BLOB, &ca);
		option(CURLOPT_CONNECTTIMEOUT, 10L);
		option(CURLOPT_TIMEOUT, 20L);
		option(CURLOPT_NOSIGNAL, 1L);
		option(CURLOPT_USERAGENT, "PS5SX2/10");
		option(CURLOPT_ERRORBUFFER, detail);
		option(CURLOPT_WRITEFUNCTION, &Receive);
		option(CURLOPT_WRITEDATA, &transfer);
		option(CURLOPT_NOPROGRESS, 0L);
		option(CURLOPT_XFERINFOFUNCTION, &Progress);
		option(CURLOPT_XFERINFODATA, this);
		// (AI-assisted) This backend needs proper testing on PS5 firmware, including cancellation.
		const double started = Now();
		const CURLcode result = configured == CURLE_OK ? curl_easy_perform(handle) : configured;
		long status = 0;
		const CURLcode got_status = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
		std::printf("[frontend] cover curl: %s -> code %d, HTTP %ld, %zu bytes, %.0f ms; %s\n",
			url.c_str(), static_cast<int>(result), status, out.size(), (Now() - started) * 1000.0, detail);
		std::fflush(stdout);
		curl_easy_cleanup(handle);
		if (result != CURLE_OK || got_status != CURLE_OK || status < 100 || status > 599)
		{
			const int code = result != CURLE_OK ? result : got_status != CURLE_OK ? got_status : -1;
			Fail("curl HTTPS", code);
			last_error += ": ";
			last_error += detail[0] ? detail : curl_easy_strerror(static_cast<CURLcode>(code));
			std::string write_error;
			fileio::WriteAtomic("/data/PS5SNES/logs/cover-download.log", last_error + "\n", write_error);
			out.clear();
			return -1;
		}
		return static_cast<int>(status);
	}
#endif
	std::string last_error;
	void Fail(const char* stage, int code)
	{
		char message[128];
		std::snprintf(message, sizeof(message), "Covers: %s 0x%08X", stage, static_cast<unsigned>(code));
		last_error = message;
		std::string write_error;
		fileio::WriteAtomic("/data/PS5SNES/logs/cover-download.log", last_error + "\n", write_error);
	}
	bool tried = false, ok = false, netctl = false;
	int pool = -1, ssl = -1, ctx = -1, tmpl = -1;
	std::atomic<int> active{-1}; // the request in flight, for Abort from the main thread
	// vk-285-110: set by Abort, so a request that was still being set up when it ran doesn't start
	// (Abort only reaches a request once `active` holds it).
	std::atomic<bool> stopping{false};
	// vk-285-110: Abort's call and the worker's delete of the same request never overlap.
	std::mutex abort_mutex;

	// The worker, done with `req`: out of `active` (after any Abort call on it), then deleted.
	void Release(int req)
	{
		{
			std::lock_guard<std::mutex> lock(abort_mutex);
			active = -1;
		}
		sceHttp2DeleteRequest(req);
	}

	bool Init()
	{
		if (tried)
			return ok;
		tried = true;
		// Is there a network at all? vk-285-41's first download never came back (100+ s): an
		// offline console must not wait on the resolver. State 3 is "IP address obtained"; a state
		// the call cannot tell leaves the decision to the request itself.
		const int nc = sceNetCtlInit();
		netctl = nc == 0;
		int state[4] = {-1, 0, 0, 0}; // room to spare, as with the time call
		const int gs = sceNetCtlGetState(state);
		std::printf("[frontend] netctl: init %#x, state %d (%#x)\n", static_cast<unsigned>(nc), state[0],
			static_cast<unsigned>(gs));
		std::fflush(stdout);
		if (gs == 0 && state[0] >= 0 && state[0] < 3)
		{
			Fail("network state", state[0]);
			std::printf("[frontend] the console is not connected; no cover downloads this time\n");
			std::fflush(stdout);
			return false;
		}
		const int net = sceNetInit(); // an error here only means it was up already
		pool = sceNetPoolCreate("pcsx2-frontend", 64 * 1024, 0);
		ssl = pool >= 0 ? sceSslInit(256 * 1024) : -1;
		ctx = ssl >= 0 ? sceHttp2Init(pool, ssl, 256 * 1024, 1) : -1;
		tmpl = ctx >= 0 ? sceHttp2CreateTemplate(ctx, "PS5SX2/1.0", 3, 1) : -1;
		std::printf("[frontend] https: net %#x pool %#x ssl %#x http2 %#x template %#x\n", static_cast<unsigned>(net),
			static_cast<unsigned>(pool), static_cast<unsigned>(ssl), static_cast<unsigned>(ctx), static_cast<unsigned>(tmpl));
		std::fflush(stdout);
		if (tmpl < 0)
		{
			if (pool < 0)
				Fail("net pool", pool);
			else if (ssl < 0)
				Fail("SSL init", ssl);
			else if (ctx < 0)
				Fail("HTTP2 init", ctx);
			else
				Fail("HTTP template", tmpl);
			return false;
		}
		ok = true;
		return true;
	}

	// Main thread, at shutdown: fails the request in flight so the worker can finish.
	void Abort()
	{
		stopping.store(true); // before reading `active`: Get stores `active`, then reads this
		std::lock_guard<std::mutex> lock(abort_mutex);
		const int req = active.load();
		if (req >= 0)
		{
			const int r = sceHttp2AbortRequest(req);
			std::printf("[frontend] aborted request %#x: %#x\n", static_cast<unsigned>(req), static_cast<unsigned>(r));
			std::fflush(stdout);
		}
	}

	// After the worker has stopped: gives the pools back.
	void Term()
	{
#ifdef ORBIS_CURL
		if (curl_ready)
			curl_global_cleanup();
		curl_ready = false;
#endif
		if (tmpl >= 0)
			sceHttp2DeleteTemplate(tmpl);
		if (ctx >= 0)
			sceHttp2Term(ctx);
		if (ssl >= 0)
			sceSslTerm(ssl);
		if (pool >= 0)
			sceNetPoolDestroy(pool);
		if (netctl)
			sceNetCtlTerm();
		tmpl = ctx = ssl = pool = -1;
		tried = ok = netctl = false;
		stopping = false;
	}

	// The HTTP status, or -1 when the request could not be made at all. Every step is logged with
	// its time: vk-285-41's first request never returned, and the log could not say where.
	int Get(const std::string& url, std::vector<uint8_t>& out)
	{
#ifdef ORBIS_CURL
		return GetCurl(url, out);
#else
		last_error.clear();
		if (stopping)
			return -1;
		if (!Init())
		{
			Term(); // (AI-assisted) A transient initial failure must not poison every later cover request.
			return -1;
		}
		const double t0 = Now();
		auto ms = [&] { return (Now() - t0) * 1000.0; };
		const int req = sceHttp2CreateRequestWithURL(tmpl, "GET", url.c_str(), 0);
		std::printf("[frontend] get %s: request %#x\n", url.c_str(), static_cast<unsigned>(req));
		std::fflush(stdout);
		if (req < 0)
		{
			Fail("request create", req);
			if (!stopping)
				Term();
			return -1;
		}
		// On the request, as Swordpdf/Twiso's working cover fetcher sets them (microseconds): each
		// phase 10 s, the whole request 20 s, redirects followed.
		const int t_resolve = sceHttp2SetResolveTimeOut(req, 10 * 1000 * 1000);
		const int t_connect = sceHttp2SetConnectTimeOut(req, 10 * 1000 * 1000);
		const int t_send = sceHttp2SetSendTimeOut(req, 10 * 1000 * 1000);
		const int t_recv = sceHttp2SetRecvTimeOut(req, 10 * 1000 * 1000);
		const int t_total = sceHttp2SetTimeOut(req, 20 * 1000 * 1000);
		const int redirect = sceHttp2SetAutoRedirect(req, 1);
		std::printf("[frontend] get: timeouts %#x %#x %#x %#x %#x, redirect %#x\n", static_cast<unsigned>(t_resolve),
			static_cast<unsigned>(t_connect), static_cast<unsigned>(t_send), static_cast<unsigned>(t_recv),
			static_cast<unsigned>(t_total), static_cast<unsigned>(redirect));
		std::fflush(stdout);
		active = req;
		if (stopping) // Abort ran while this request was being set up, before `active` held it
		{
			Release(req);
			std::printf("[frontend] get: stopped before sending\n");
			std::fflush(stdout);
			return -1;
		}
		int status = -1;
		const int sent = sceHttp2SendRequest(req, nullptr, 0);
		const int got = sent == 0 ? sceHttp2GetStatusCode(req, &status) : -1;
		std::printf("[frontend] get: send %#x, status %d (%#x) after %.0f ms\n", static_cast<unsigned>(sent), status,
			static_cast<unsigned>(got), ms());
		std::fflush(stdout);
		if (sent != 0 || got != 0)
		{
			Fail(sent != 0 ? "HTTP send" : "HTTP status", sent != 0 ? sent : got);
			status = -1;
		}
		else if (status < 100 || status > 599)
		{
			Fail("invalid HTTP status", status);
			status = -1;
		}
		else if (status == 200)
		{
			std::vector<uint8_t> buf(64 * 1024);
			for (;;)
			{
				const int n = sceHttp2ReadData(req, buf.data(), buf.size());
				if (n < 0)
				{
					Fail("HTTP read", n);
					std::printf("[frontend] get: read %#x after %zu bytes\n", static_cast<unsigned>(n), out.size());
					status = -1;
					break;
				}
				if (n == 0)
					break;
				out.insert(out.end(), buf.begin(), buf.begin() + n);
				if (out.size() > (16u << 20))
				{
					Fail("response too large", -1);
					status = -1;
					break;
				}
			}
			std::printf("[frontend] get: %zu bytes in %.0f ms\n", out.size(), ms());
			std::fflush(stdout);
		}
		Release(req);
		if (status < 0 && !stopping)
			Term(); // allow the cover worker's bounded retry to create a fresh HTTPS context
		return status;
#endif
	}
};
Http g_http;

// ---- The display ----
struct Display
{
	Vk vk;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice pd = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t qf = 0;
	VkQueue queue = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> images;
	VkExtent2D extent = {};
	VkSemaphore acquired[2] = {}, rendered[2] = {};

	bool Fail(const char* what, VkResult r)
	{
		std::printf("[frontend] %s failed: %d\n", what, static_cast<int>(r));
		std::fflush(stdout);
		return false;
	}

	bool Init()
	{
		const char* missing = nullptr;
		if (!vk.LoadGlobal(vk_icdGetInstanceProcAddr, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		const char* inst_ext[2] = {"VK_KHR_surface", "VK_KHR_display"};
		VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		ai.pApplicationName = "PS5SX2 frontend";
		ai.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		ici.pApplicationInfo = &ai;
		ici.enabledExtensionCount = 2;
		ici.ppEnabledExtensionNames = inst_ext;
		VkResult r = vk.vkCreateInstance(&ici, nullptr, &instance);
		if (r != VK_SUCCESS)
			return Fail("vkCreateInstance", r);
		if (!vk.LoadInstance(instance, true, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		uint32_t count = 1;
		r = vk.vkEnumeratePhysicalDevices(instance, &count, &pd);
		if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || count == 0)
			return Fail("vkEnumeratePhysicalDevices", r);
		uint32_t qcount = 0;
		vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
		std::vector<VkQueueFamilyProperties> qfs(qcount);
		vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qfs.data());
		qf = UINT32_MAX;
		for (uint32_t i = 0; i < qcount; i++)
			if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				qf = i;
				break;
			}
		if (qf == UINT32_MAX)
			return Fail("a graphics queue", VK_ERROR_UNKNOWN);
		const float prio = 1.0f;
		VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = qf;
		qi.queueCount = 1;
		qi.pQueuePriorities = &prio;
		const char* dev_ext[1] = {"VK_KHR_swapchain"};
		VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qi;
		dci.enabledExtensionCount = 1;
		dci.ppEnabledExtensionNames = dev_ext;
		r = vk.vkCreateDevice(pd, &dci, nullptr, &device);
		if (r != VK_SUCCESS)
			return Fail("vkCreateDevice", r);
		if (!vk.LoadDevice(device, true, &missing))
		{
			std::printf("[frontend] no %s\n", missing);
			return false;
		}
		vk.vkGetDeviceQueue(device, qf, 0, &queue);
		return CreateSurface() && CreateSwapchain();
	}

	// As PCSX2's VKSwapChain does on the PS5: the first display with a mode, its largest mode (of equally
	// large ones the one nearest 60 Hz, vk-285-115), and a plane that can drive it.
	bool CreateSurface()
	{
		uint32_t display_count = 0;
		if (vk.vkGetPhysicalDeviceDisplayPropertiesKHR(pd, &display_count, nullptr) != VK_SUCCESS || !display_count)
			return Fail("vkGetPhysicalDeviceDisplayPropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPropertiesKHR> displays(display_count);
		vk.vkGetPhysicalDeviceDisplayPropertiesKHR(pd, &display_count, displays.data());
		uint32_t plane_count = 0;
		if (vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(pd, &plane_count, nullptr) != VK_SUCCESS || !plane_count)
			return Fail("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(pd, &plane_count, planes.data());
		VkDisplayKHR display = VK_NULL_HANDLE;
		VkDisplayModePropertiesKHR mode = {};
		for (const VkDisplayPropertiesKHR& d : displays)
		{
			uint32_t mode_count = 0;
			if (vk.vkGetDisplayModePropertiesKHR(pd, d.display, &mode_count, nullptr) != VK_SUCCESS || !mode_count)
				continue;
			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			vk.vkGetDisplayModePropertiesKHR(pd, d.display, &mode_count, modes.data());
			auto off_60hz = [](const VkDisplayModePropertiesKHR& x) {
				const int64_t mhz = static_cast<int64_t>(x.parameters.refreshRate);
				return mhz > 60000 ? mhz - 60000 : 60000 - mhz;
			};
			for (const VkDisplayModePropertiesKHR& m : modes)
			{
				const uint64_t area = static_cast<uint64_t>(m.parameters.visibleRegion.width) * m.parameters.visibleRegion.height;
				const uint64_t best = static_cast<uint64_t>(mode.parameters.visibleRegion.width) * mode.parameters.visibleRegion.height;
				if (!display || area > best || (area == best && off_60hz(m) < off_60hz(mode)))
				{
					display = d.display;
					mode = m;
				}
			}
			if (display)
				break;
		}
		if (!display)
			return Fail("a display mode", VK_ERROR_UNKNOWN);
		uint32_t plane = UINT32_MAX;
		for (uint32_t i = 0; i < plane_count; i++)
			if (planes[i].currentDisplay == VK_NULL_HANDLE || planes[i].currentDisplay == display)
			{
				plane = i;
				break;
			}
		if (plane == UINT32_MAX)
			return Fail("a display plane", VK_ERROR_UNKNOWN);
		const VkDisplaySurfaceCreateInfoKHR ci = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR, nullptr, 0,
			mode.displayMode, plane, planes[plane].currentStackIndex, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1.0f,
			VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, mode.parameters.visibleRegion};
		const VkResult r = vk.vkCreateDisplayPlaneSurfaceKHR(instance, &ci, nullptr, &surface);
		if (r != VK_SUCCESS)
			return Fail("vkCreateDisplayPlaneSurfaceKHR", r);
		extent = mode.parameters.visibleRegion;
		std::printf("[frontend] display %ux%u at %.2f Hz, plane %u\n", extent.width, extent.height, mode.parameters.refreshRate / 1000.0,
			plane);
		return true;
	}

	bool CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR caps = {};
		VkResult r = vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);
		if (r != VK_SUCCESS)
			return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);
		if (caps.currentExtent.width != UINT32_MAX)
			extent = caps.currentExtent;
		uint32_t images_wanted = std::max(2u, caps.minImageCount);
		if (caps.maxImageCount)
			images_wanted = std::min(images_wanted, caps.maxImageCount);
		VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
		sci.surface = surface;
		sci.minImageCount = images_wanted;
		sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
		sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		sci.imageExtent = extent;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		sci.clipped = VK_TRUE;
		r = vk.vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain);
		if (r != VK_SUCCESS)
			return Fail("vkCreateSwapchainKHR", r);
		uint32_t n = 0;
		vk.vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
		images.resize(n);
		vk.vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());
		VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		for (int i = 0; i < 2; i++)
			if (vk.vkCreateSemaphore(device, &si, nullptr, &acquired[i]) != VK_SUCCESS ||
				vk.vkCreateSemaphore(device, &si, nullptr, &rendered[i]) != VK_SUCCESS)
				return Fail("vkCreateSemaphore", VK_ERROR_UNKNOWN);
		std::printf("[frontend] swapchain: %u images %ux%u\n", n, extent.width, extent.height);
		return true;
	}

	void Destroy()
	{
		if (device)
		{
			vk.vkDeviceWaitIdle(device);
			for (int i = 0; i < 2; i++)
			{
				if (acquired[i])
					vk.vkDestroySemaphore(device, acquired[i], nullptr);
				if (rendered[i])
					vk.vkDestroySemaphore(device, rendered[i], nullptr);
				acquired[i] = rendered[i] = VK_NULL_HANDLE;
			}
			if (swapchain)
				vk.vkDestroySwapchainKHR(device, swapchain, nullptr); // closes VideoOut for PCSX2
			swapchain = VK_NULL_HANDLE;
			vk.vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
		if (surface)
			vk.vkDestroySurfaceKHR(instance, surface, nullptr);
		surface = VK_NULL_HANDLE;
		if (instance)
			vk.vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}
};

std::string ReadLastGame(const std::string& dir)
{
	std::string name;
	if (FILE* f = std::fopen((dir + "/lastgame.txt").c_str(), "r"))
	{
		char buf[512] = {};
		if (std::fgets(buf, sizeof(buf), f))
			name = buf;
		std::fclose(f);
	}
	while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
		name.pop_back();
	return name;
}

void WriteLastGame(const std::string& dir, const std::string& file)
{
	if (FILE* f = std::fopen((dir + "/lastgame.txt").c_str(), "w"))
	{
		std::fprintf(f, "%s\n", file.c_str());
		std::fclose(f);
	}
}

// ---- Test build 1 (vk-285-55): games on USB drives.

std::string LowerAscii(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

// The disc images (.iso, .chd) straight in `dir`, or -1 when it can't be opened as a folder.
int CountImages(const std::string& dir)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return -1;
	int n = 0;
	while (const dirent* e = readdir(d))
		n += IsDiscImageName(e->d_name) ? 1 : 0;
	closedir(d);
	return n;
}

bool OnUsb(const std::string& path)
{
	return path.compare(0, 8, "/mnt/usb") == 0;
}

// cache/usb-games.txt, one "<serial>\t<stem>\t<title>" line per USB game: before the jailbreak,
// where covers download, the app may not see the drives, so the covers of the games found on them
// after the jailbreak are fetched at the next start from this list.
void WriteUsbList(const std::string& path, const std::vector<GameInfo>& games)
{
	if (path.empty())
		return;
	std::string text;
	for (const GameInfo& g : games)
		if (OnUsb(g.path) && !g.serial.empty())
			text += g.serial + "\t" + g.stem + "\t" + g.title + "\n";
	std::string old;
	if (FILE* f = std::fopen(path.c_str(), "rb"))
	{
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			old.append(buf, n);
		std::fclose(f);
	}
	if (text.empty() || text == old)
		return;
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos)
		mkdir(path.substr(0, slash).c_str(), 0777);
	if (FILE* f = std::fopen(path.c_str(), "wb"))
	{
		std::fwrite(text.data(), 1, text.size(), f);
		std::fclose(f);
	}
}

// Adds the listed USB games that `games` doesn't hold (by serial), with no image path: enough for the
// cover prefetch, which works from serials.
int AddUsbListGames(const std::string& path, std::vector<GameInfo>& games)
{
	FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
	if (!f)
		return 0;
	int added = 0;
	char line[1024];
	while (std::fgets(line, sizeof(line), f))
	{
		std::string l = line;
		while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
			l.pop_back();
		const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
		if (t2 == std::string::npos || t1 == 0)
			continue;
		GameInfo g;
		g.serial = l.substr(0, t1);
		g.stem = l.substr(t1 + 1, t2 - t1 - 1);
		g.title = l.substr(t2 + 1);
		if (std::any_of(games.begin(), games.end(), [&](const GameInfo& x) { return x.serial == g.serial; }))
			continue;
		games.push_back(g);
		added++;
	}
	std::fclose(f);
	return added;
}
} // namespace

std::vector<std::string> orbis_usb_game_dirs(const char* when)
{
	std::vector<std::string> dirs;
	int drives = 0;
	for (int i = 0; i < 8; i++)
	{
		char root[16];
		std::snprintf(root, sizeof(root), "/mnt/usb%d", i);
		DIR* d = opendir(root);
		if (!d)
		{
			if (when && errno != ENOENT)
				std::printf("[usb] %s: %s: can't open (errno %d)\n", when, root, errno);
			continue;
		}
		int entries = 0, isos = 0;
		std::vector<std::string> subs;
		while (const dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.')
				continue;
			entries++;
			isos += IsDiscImageName(e->d_name) ? 1 : 0;
			const std::string lower = LowerAscii(e->d_name);
			// (AI-assisted) Also find homebrew/PCSX2/games on USB; needs proper testing on the console.
			if (lower == "homebrew")
			{
				const std::string homebrew = std::string(root) + "/" + e->d_name;
				if (DIR* hd = opendir(homebrew.c_str()))
				{
					while (const dirent* he = readdir(hd))
					{
						if (LowerAscii(he->d_name) != "pcsx2")
							continue;
						const std::string parent = homebrew + "/" + he->d_name;
						if (DIR* pd = opendir(parent.c_str()))
						{
							while (const dirent* pe = readdir(pd))
							{
								if (LowerAscii(pe->d_name) == "games")
									subs.push_back(std::string(e->d_name) + "/" + he->d_name + "/" + pe->d_name);
							}
							closedir(pd);
						}
					}
					closedir(hd);
				}
			}
			if (lower == "dvd" || lower == "cd" || lower == "ps5sx2" || lower == "games")
				subs.push_back(e->d_name);
			// vk-285-118 (AI-assisted): a drive laid out like /data/PCSX2 (testers: "games in the games/ folder on the external
			// drive are NOT detected", while PCSX2/textures there works): PCSX2/games/, and PS5SX2/games/, also DVD/ and CD/ in them.
			if (lower == "pcsx2" || lower == "ps5sx2")
			{
				const std::string parent = std::string(root) + "/" + e->d_name;
				if (DIR* pd = opendir(parent.c_str()))
				{
					while (const dirent* pe = readdir(pd))
					{
						const std::string l2 = LowerAscii(pe->d_name);
						if (l2 == "games" || l2 == "dvd" || l2 == "cd")
							subs.push_back(std::string(e->d_name) + "/" + pe->d_name);
					}
					closedir(pd);
				}
			}
		}
		closedir(d);
		if (entries == 0)
			continue; // no drive in this slot (the folder is there either way)
		drives++;
		dirs.push_back(root);
		std::string found = "/ (" + std::to_string(isos) + ")";
		for (const std::string& sub : subs)
		{
			const std::string dir = std::string(root) + "/" + sub;
			const int n = CountImages(dir);
			if (n < 0)
				continue;
			dirs.push_back(dir);
			found += ", " + sub + "/ (" + std::to_string(n) + ")";
		}
		if (when)
			std::printf("[usb] %s: %s: %d entries; disc images in %s\n", when, root, entries, found.c_str());
	}
	if (when && drives == 0)
		std::printf("[usb] %s: no USB drive with files at /mnt/usb0-7\n", when);
	if (when)
		std::fflush(stdout);
	return dirs;
}

bool orbis_frontend_watermark(const char* line1, const char* line2, const char* line3, float alpha1, float alpha2,
	std::vector<uint32_t>& rgba, int& w, int& h)
{
	Fonts fonts;
	if (!fonts.Init(fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text), fe_font_icons,
			static_cast<size_t>(fe_font_icons_end - fe_font_icons)))
		return false;
	RasterWatermark(fonts, line1, line2, line3, alpha1, alpha2, rgba, w, h);
	return true;
}

// vk-285-50: the settings page's server; it lives until the app ends.
static fe::WebServer* g_web = nullptr;

// vk-285-51: the settings log's clock, in the console's time zone (as Clock() above).
static long long SettingsLogLocalTime(long long utc)
{
	time_t local = static_cast<time_t>(utc);
	KernelTimesec sec[2] = {};
	uint64_t dst[2] = {};
	if (sceKernelConvertUtcToLocaltime(static_cast<time_t>(utc), &local, &sec[0], &dst[0]) != 0)
		local = static_cast<time_t>(utc);
	return static_cast<long long>(local);
}

// vk-285-53: sceSystemServiceHideSplashScreen once per process (later calls return the first result).
int orbis_hide_splash()
{
	static std::atomic<int> s_result{1};
	int expected = 1;
	if (s_result.compare_exchange_strong(expected, 2))
		s_result.store(sceSystemServiceHideSplashScreen());
	return s_result.load();
}

int orbis_ps5_language()
{
	static int s_lang = -2;
	if (s_lang == -2)
	{
		int v = 1;
		const int rc = sceSystemServiceParamGetInt(1 /* language */, &v);
		s_lang = rc == 0 ? v : 1;
		std::printf("[i18n] system language: rc=%x value=%d\n", static_cast<unsigned>(rc), v);
		std::fflush(stdout);
	}
	return s_lang;
}

void orbis_frontend_set_language(const std::string& lang_dir)
{
	fe::SetLanguage(orbis_ps5_language(), lang_dir);
}

// vk-285-113: the settings log on a console that shows /data only after the jailbreak. The path given at the start
// is then the top folder's (logs/ isn't visible yet) and every line written before the jailbreak was lost, the
// "app start" line with them (v112's console ...575 on firmware 10.20: sessions with no build and no game). Now:
//   - logs/ is looked for at every write, so the file is logs/settings.log as soon as that folder is there;
//   - a line that can't be written yet waits in a small buffer and goes out, in order, with the next one that can;
//   - the first write of a run first looks at the end of the file for the previous run's closing line and, when
//     there is none, says so: the app was closed from the PS button, the system killed it, the console lost power,
//     or it crashed without a signal.
// All of it is open()/pread()/write() on stack buffers, so the crash handler and the exit hooks can still call it.
static char g_event_log_path[256];     // as given
static char g_event_log_dir_logs[256]; // <dir>/logs, checked at each write ("" when the path already is inside a logs folder)
static char g_event_log_in_logs[256];  // <dir>/logs/<name>
static char g_event_pending[8192];     // formatted lines waiting for a folder
static size_t g_event_pending_len = 0;
static std::atomic<bool> g_event_busy{false};
static bool g_event_previous_checked = false;

void orbis_event_log_init(const std::string& path)
{
	std::snprintf(g_event_log_path, sizeof(g_event_log_path), "%s", path.c_str());
	g_event_log_dir_logs[0] = g_event_log_in_logs[0] = 0;
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos && slash > 0)
	{
		const std::string dir = path.substr(0, slash);
		const size_t dslash = dir.rfind('/');
		const std::string last = dslash == std::string::npos ? dir : dir.substr(dslash + 1);
		if (last != "logs")
		{
			std::snprintf(g_event_log_dir_logs, sizeof(g_event_log_dir_logs), "%s/logs", dir.c_str());
			std::snprintf(g_event_log_in_logs, sizeof(g_event_log_in_logs), "%s/logs/%s", dir.c_str(), path.c_str() + slash + 1);
		}
	}
	fe::g_utc_to_local = &SettingsLogLocalTime;
}

// Whether a log line's text (after the time) says the run that wrote it is over. The lines the app itself writes
// as it ends: main-boot.cpp's exits, the crash printer, the GPU-hang exit and the new signal and exit hooks.
static bool EventEndsARun(const char* text)
{
	static const char* const kEnds[] = {"back to the menu", "the app closed", "GPU hang", "crash:", "no game to start",
		"the game didn't start", "the system ended the app", "app exit"};
	for (const char* e : kEnds)
		if (std::strstr(text, e))
			return true;
	return false;
}

// The note about the previous run, or 0 when it closed itself properly (or there is no earlier log).
static int EventPreviousRunNote(int fd, char* out, size_t out_size)
{
	struct stat st = {};
	if (fstat(fd, &st) != 0 || st.st_size <= 0)
		return 0;
	char tail[2048];
	const off_t from = st.st_size > static_cast<off_t>(sizeof(tail)) ? st.st_size - static_cast<off_t>(sizeof(tail)) : 0;
	const ssize_t got = pread(fd, tail, sizeof(tail) - 1, from);
	if (got <= 0)
		return 0;
	tail[got] = 0;
	// the last line: after the last newline that isn't the final character
	ssize_t end = got;
	while (end > 0 && (tail[end - 1] == '\n' || tail[end - 1] == '\r'))
		end--;
	if (end <= 0)
		return 0;
	ssize_t begin = end;
	while (begin > 0 && tail[begin - 1] != '\n')
		begin--;
	tail[end] = 0;
	const char* last = tail + begin;
	// "YYYY-MM-DD HH:MM:SS  text"
	const bool stamped = end - begin > 21 && last[4] == '-' && last[7] == '-' && last[10] == ' ' && last[13] == ':' && last[16] == ':';
	if (stamped && EventEndsARun(last + 21))
		return 0;
	if (!stamped)
		return 0; // not a line of ours
	return std::snprintf(out, out_size,
		"previous run: its log ends without a closing line (last entry %.19s: \"%.50s\"): the app was closed from the PS button or "
		"by the system, the console lost power, or the app crashed without a signal",
		last, last + 21);
}

extern "C" void orbis_event_log(const char* line)
{
	if (!g_event_log_path[0] || !line)
		return;
	const time_t local = static_cast<time_t>(SettingsLogLocalTime(static_cast<long long>(time(nullptr))));
	struct tm tm = {};
	gmtime_r(&local, &tm);
	char buf[1280];
	int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d  %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, line);
	if (n <= 0)
		return;
	if (n >= static_cast<int>(sizeof(buf)))
	{
		n = static_cast<int>(sizeof(buf)) - 1;
		buf[n - 1] = '\n';
	}
	// logs/ once it is there, else the path as given.
	const char* path = g_event_log_path;
	if (g_event_log_dir_logs[0])
	{
		struct stat st = {};
		if (stat(g_event_log_dir_logs, &st) == 0 && S_ISDIR(st.st_mode))
			path = g_event_log_in_logs;
	}
	// One thread at a time in the buffer; a caller that finds it taken (another thread, or a signal that interrupted
	// it) writes its line by itself and leaves the buffer alone.
	const bool own = !g_event_busy.exchange(true, std::memory_order_acquire);
	const int fd = open(path, O_RDWR | O_APPEND | O_CREAT, 0666);
	if (fd < 0)
	{
		if (own)
		{
			if (g_event_pending_len + static_cast<size_t>(n) <= sizeof(g_event_pending))
			{
				std::memcpy(g_event_pending + g_event_pending_len, buf, static_cast<size_t>(n));
				g_event_pending_len += static_cast<size_t>(n);
			}
			g_event_busy.store(false, std::memory_order_release);
		}
		return;
	}
	if (own && !g_event_previous_checked)
	{
		g_event_previous_checked = true;
		char note[400];
		const int m = EventPreviousRunNote(fd, note + 0, sizeof(note) - 1);
		if (m > 0)
		{
			char stamped[480];
			const int k = std::snprintf(stamped, sizeof(stamped), "%04d-%02d-%02d %02d:%02d:%02d  %s\n", tm.tm_year + 1900, tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, note);
			if (k > 0)
				(void)!write(fd, stamped, static_cast<size_t>(k < static_cast<int>(sizeof(stamped)) ? k : sizeof(stamped) - 1));
		}
	}
	if (own && g_event_pending_len)
	{
		(void)!write(fd, g_event_pending, g_event_pending_len);
		g_event_pending_len = 0;
	}
	(void)!write(fd, buf, static_cast<size_t>(n));
	close(fd);
	if (own)
		g_event_busy.store(false, std::memory_order_release);
}

bool orbis_web_start(const OrbisFrontendPaths& paths, const char* build_tag)
{
	if (g_web)
		return true;
	SetSerialCacheFile(paths.serial_cache); // vk-285-108
	SetGameDbFile(paths.gamedb_file);       // vk-285-113
	WebConfig cfg;
	cfg.game_dirs = {paths.games_dir, paths.top_dir};
	cfg.game_dirs.insert(cfg.game_dirs.end(), paths.usb_dirs.begin(), paths.usb_dirs.end()); // test build 1
	cfg.settings_dir = paths.settings_dir;
	cfg.gs_ini = paths.gs_ini;
	cfg.patches_dir = paths.patches_dir;
	cfg.covers_dir = paths.covers_dir;
	cfg.cache_dir = paths.cache_dir;
	cfg.build_tag = build_tag ? build_tag : "";
	cfg.port = 8844;
	cfg.presets.clear(); // (AI-assisted) NES defaults come from the NES option groups.
	cfg.change_log = paths.settings_log;
	// Test build 1 (vk-285-55): the logs download.
	cfg.logs_dir = paths.logs_dir;
	cfg.top_dir = paths.top_dir;
	cfg.memcards_dir = paths.memcards_dir; // vk-285-113
	cfg.report_header = paths.report_header;
	cfg.test_build = paths.test_build;
	fe::g_utc_to_local = &SettingsLogLocalTime;
	cfg.assets = {
		{"/", "text/html; charset=utf-8", fe_web_page, static_cast<size_t>(fe_web_page_end - fe_web_page)},
		{"/fonts/roboto.ttf", "font/ttf", fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text)},
		{"/icons/discord.svg", "image/svg+xml", fe_web_discord, static_cast<size_t>(fe_web_discord_end - fe_web_discord)},
		{"/icons/x-twitter.svg", "image/svg+xml", fe_web_x, static_cast<size_t>(fe_web_x_end - fe_web_x)},
	};
	fe::WebServer* server = new fe::WebServer();
	if (!server->Start(cfg))
	{
		std::printf("[web] the settings page could not start\n");
		std::fflush(stdout);
		delete server;
		return false;
	}
	g_web = server;
	std::string url, shown;
	std::printf("[web] %s\n", g_web->Address(url, shown) ? ("http://" + shown).c_str() : "no network address yet");
	std::fflush(stdout);
	return true;
}

void orbis_web_now_playing(const std::string& image_path)
{
	if (g_web)
		g_web->SetNowPlaying(image_path);
}

// vk-285-113: the page's address as a QR code (the one the shelf shows) for the panel drawn over the game.
bool orbis_web_qr(std::vector<unsigned char>& modules, int& size, std::string& shown)
{
	if (!g_web)
		return false;
	std::string url;
	if (!g_web->Address(url, shown))
		return false;
	uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
	if (!qrcodegen_encodeText(url.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
		return false;
	size = qrcodegen_getSize(qr);
	modules.assign(static_cast<size_t>(size) * size, 0);
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			modules[static_cast<size_t>(y) * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
	return true;
}

bool orbis_web_browser_url(std::string& url, std::string& shown)
{
	if (!g_web)
		return false;
	if (g_web->Address(url, shown))
		return true;
	if (g_web->Port() == 0)
		return false;
	url = g_web->LoopbackUrl();
	shown = "127.0.0.1:" + std::to_string(g_web->Port());
	return true;
}

void orbis_web_request_stats(uint64_t& count, double& age_s)
{
	count = 0;
	age_s = -1.0;
	if (g_web)
		g_web->RequestStats(count, age_s);
}

void orbis_web_log_next_requests(int n)
{
	if (g_web)
		g_web->LogNextRequests(n);
}

int orbis_frontend_prefetch_covers(const OrbisFrontendPaths& paths, double budget_s, void (*notify)(const char*))
{
	if (!paths.allow_download)
		return 0;
	const double t0 = Now();
	SetSerialCacheFile(paths.serial_cache); // vk-285-108
	SetGameDbFile(paths.gamedb_file);       // vk-285-113
	std::vector<std::string> dirs = {paths.games_dir, paths.top_dir};
	dirs.insert(dirs.end(), paths.usb_dirs.begin(), paths.usb_dirs.end()); // test build 1: USB drives, when visible here
	std::vector<GameInfo> games = ScanGames(dirs);
	for (GameInfo& g : games)
		g.serial = ReadSerial(g.path);
	// Test build 1: the USB games seen after the jailbreak last time (the drives may not be visible here).
	const int listed = AddUsbListGames(paths.usb_list, games);
	CoverConfig cc;
    cc.window_radius=12;
	cc.manual_dir = paths.covers_dir;
	cc.cache_dir = paths.cache_dir;
	cc.url_template = kCoverUrl;
    cc.resolve_urls=snes::CoverUrls;
	const std::vector<int> missing = CoverService::MissingCovers(games, cc);
	std::printf("[frontend] prefetch: %zu of %zu covers to fetch (%d from the USB list; checked in %.0f ms)\n", missing.size(),
		games.size(), listed, (Now() - t0) * 1000.0);
	std::fflush(stdout);
	if (missing.empty())
		return 0;
	if (notify)
	{
		char buf[96];
		std::snprintf(buf, sizeof(buf), fe::Tr(missing.size() == 1 ? fe::Str::NotifyCoversOne : fe::Str::NotifyCoversMany),
			static_cast<int>(missing.size())); // vk-285-110
		notify(buf);
	}
	const int saved = CoverService::Prefetch(games, missing, cc,
		[](const std::string& url, std::vector<uint8_t>& out) { return g_http.Get(url, out); }, budget_s);
	g_http.Term();
	std::printf("[frontend] prefetch: %d saved in %.1f s\n", saved, Now() - t0);
	std::fflush(stdout);
	return saved;
}

std::string orbis_frontend_run(const OrbisFrontendPaths& paths, const char* build_tag, bool* ran)
{
	*ran = false;
	const double t0 = Now();
	SetSerialCacheFile(paths.serial_cache); // vk-285-108
	SetGameDbFile(paths.gamedb_file);       // vk-285-113
	std::vector<std::string> dirs = {paths.games_dir, paths.top_dir};
	dirs.insert(dirs.end(), paths.usb_dirs.begin(), paths.usb_dirs.end()); // test build 1: USB drives
	int32_t user = -1;
	(void)sceUserServiceInitialize(nullptr);
	(void)sceUserServiceGetInitialUser(&user);
	(void)scePadInit();
	const int pad = user >= 0 ? scePadOpen(user, 0, 0, nullptr) : -1;
	if (pad < 0)
	{
		std::printf("[frontend] no controller (%d); the plain list takes over\n", pad);
		return {};
	}

	// Heap objects, so a download still running at the end can be left to finish on its own.
	Display* display = new Display();
	if (!display->Init())
	{
		display->Destroy();
		delete display;
		scePadClose(pad);
		return {};
	}
	Fonts* fonts = new Fonts();
	Renderer renderer;
	if (!fonts->Init(fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text), fe_font_icons,
			static_cast<size_t>(fe_font_icons_end - fe_font_icons), fe_font_brands,
			static_cast<size_t>(fe_font_brands_end - fe_font_brands)) ||
		!renderer.Init(&display->vk, display->pd, display->device, display->qf, display->queue, display->extent.width,
			display->extent.height, VK_FORMAT_B8G8R8A8_UNORM, display->images, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
	{
		std::printf("[frontend] renderer: %s\n", renderer.error().c_str());
		renderer.Shutdown();
		display->Destroy();
		delete display;
		delete fonts;
		scePadClose(pad);
		return {};
	}

    // (AI-assisted) Present the first loading frame before any slow filesystem/NFS calls.
    auto* loading_atlas=renderer.CreateTexture(fonts->AtlasWidth(),fonts->AtlasHeight(),VK_FORMAT_R8_UNORM,fonts->AtlasPixels().data());
    if(!loading_atlas) { renderer.Shutdown();display->Destroy();delete display;delete fonts;scePadClose(pad);return {}; }
    renderer.SetAtlas(loading_atlas);
    snes::LoadingState loading;
    unsigned loading_slot=0;
    auto show_loading=[&](const snes::ScanProgress& progress,bool done) {
        FrameDesc picture;snes::BuildLoadingFrame(picture,*fonts,display->extent.width,display->extent.height,progress,Now()-t0,done);
        if(!renderer.WaitForSlot())return false;
        uint32_t image=0;
        VkResult result=display->vk.vkAcquireNextImageKHR(display->device,display->swapchain,UINT64_MAX,display->acquired[loading_slot],VK_NULL_HANDLE,&image);
        if(result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)return false;
        if(!renderer.Render(picture,image,display->acquired[loading_slot],display->rendered[loading_slot]))return false;
        VkPresentInfoKHR present={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&display->rendered[loading_slot];
        present.swapchainCount=1;present.pSwapchains=&display->swapchain;present.pImageIndices=&image;
        result=display->vk.vkQueuePresentKHR(display->queue,&present);loading_slot^=1;
        return result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR;
    };
    bool loading_ok=show_loading({},false);
    if(loading_ok) {
        orbis_hide_splash();
        std::printf("[loading] first loading frame displayed before NFS scan\n");std::fflush(stdout);
    }
    if(!loading_ok) { renderer.Shutdown();display->Destroy();delete display;delete fonts;scePadClose(pad);return {}; }
    std::thread scanner([&] {
        snes::SetScanProgress([&](const snes::ScanProgress& progress){loading.Publish(progress);});
        auto found=ScanGames(dirs);snes::SetScanProgress({});
        std::lock_guard<std::mutex> lock(loading.mutex);loading.games=std::move(found);loading.done=true;
    });
    bool scanning_done=false;
    while(loading_ok) {
        auto progress=loading.Snapshot(scanning_done);
        loading_ok=show_loading(progress,scanning_done);
        if(scanning_done) {
            // Show the reactor's completed / online animation briefly,
            // even when a small library loads almost instantaneously.
            const double ready_until=Now()+0.32;
            while (loading_ok && Now()<ready_until) {
                loading_ok=show_loading(progress,true);
                usleep(16000);
            }
            break;
        }
        // UI presentation remains on this thread; NFS only runs on the scanner.
        usleep(1000);
    }
    scanner.join();
    renderer.WaitIdle();renderer.SetAtlas(nullptr);renderer.DestroyTexture(loading_atlas);
    if(!loading_ok) { renderer.Shutdown();display->Destroy();delete display;delete fonts;scePadClose(pad);return {}; }
	std::vector<GameInfo> games = std::move(loading.games);
	int on_usb = 0;
	for (GameInfo& g : games)
	{
		g.serial = ReadSerial(g.path);
		// vk-285-113: a CHD (or an ISO) named after its file shows the game's name from the game database.
		const std::string file_title = g.title;
		const bool renamed = ApplyGameDbTitle(g);
		ReadBadges(g, paths.settings_dir, paths.gs_ini, paths.patches_dir);
		on_usb += OnUsb(g.path) ? 1 : 0;
		if (renamed)
			std::printf("[frontend] %s: titled \"%s\" (the file name gave \"%s\")\n", g.file.c_str(), g.title.c_str(), file_title.c_str());
		// Test build 1: the size too (a disc image of an odd size is often a bad dump), and the folder.
		if (&g - games.data() < 3) std::printf("[frontend] %s | %s | %s | %llu bytes | %s\n", g.file.c_str(), g.serial.empty() ? "no serial" : g.serial.c_str(),
			g.title.c_str(), static_cast<unsigned long long>(g.bytes), g.path.substr(0, g.path.rfind('/')).c_str());
		// vk-285-109: a CHD without a serial says what it is.
		if (g.serial.empty())
		{
			const std::string what = DescribeImage(g.path);
			if (!what.empty())
				std::printf("[frontend]   chd: %s\n", what.c_str());
		}
	}
	SortGames(games); // vk-285-113: by the titles the game database may have changed
	std::printf("[frontend] %zu disc image(s), %d on USB, scanned in %.0f ms\n", games.size(), on_usb, (Now() - t0) * 1000.0);
	std::fflush(stdout);
	WriteUsbList(paths.usb_list, games);
#ifndef ORBIS_NFS
	if (games.empty())
	{
		*ran = true;
		return {};
	}
#endif
	// (AI-assisted) Keep an empty NFS-enabled shelf open so the first share can be configured.
	const std::string last = ReadLastGame(paths.top_dir);
	int preselect = 0;
	for (size_t i = 0; i < games.size(); i++)
		if (games[i].file == last)
			preselect = static_cast<int>(i);
	// vk-285-69: one game opens the shelf too, not the game straight away: the shelf's QR code is how
	// testers reach the settings page and its Download logs (Spyros, 2026-09-26).


	CoverService* covers = new CoverService();
	// (AI-assisted) Prefetch ran before filesystem elevation; do not reuse its cached HTTPS failure/context.
	g_http.Term();
	CoverConfig cc;
    cc.window_radius=12;
	cc.manual_dir = paths.covers_dir;
	cc.cache_dir = paths.cache_dir;
	cc.url_template = kCoverUrl;
    cc.resolve_urls=snes::CoverUrls;
	cc.allow_download = paths.allow_download;
	cc.download_usb_only = false; // Auto-fetch missing covers for local, USB and NFS games.
	std::printf("[covers] automatic downloads=%s; on-disk cache=%s; near-game priority radius=%d\n",
		cc.allow_download ? "ON" : "OFF", cc.cache_dir.c_str(), cc.window_radius);
	cc.download_error = [] { return g_http.last_error.empty() ? std::string("Covers: transport error") : g_http.last_error; };
	std::printf("[frontend] starting bounded cover worker\n");
	covers->Start(games, fonts, cc, [](const std::string& url, std::vector<uint8_t>& out) { return g_http.Get(url, out); });

	// The actual Porpoise Gfx renderer, not an NES case shader approximation.
	// This is opt-in at runtime: if assets, shaders or GPU init fail, the
	// previously working NES CoverFlow remains the safe fallback.
	ExactPorpoiseShelf exact;
	porpoise::ui::GfxInit original_ui{};
	original_ui.instance=display->instance;
	original_ui.gpu=display->pd;
	original_ui.device=display->device;
	original_ui.queue=display->queue;
	original_ui.queue_family=display->qf;
	original_ui.render_pass=renderer.ExactFinalPass();
	original_ui.slots=2;
	original_ui.get_instance_proc=vk_icdGetInstanceProcAddr;
	original_ui.get_device_proc=display->vk.vkGetDeviceProcAddr;
	original_ui.asset_dir=paths.top_dir+"/porpoise-ui";
	const bool assets_ready=PreparePorpoiseAssets(original_ui.asset_dir);
	original_ui.cache_dir=paths.cache_dir+"/porpoise";
	const bool exact_ready=assets_ready && exact.Init(original_ui,games);
	if(exact_ready)
		renderer.SetExactCallbacks(
			[&exact](unsigned s,float w,float h){exact.Prepare(s,w,h);},
			[&exact](VkCommandBuffer cmd){exact.Upload(cmd);},
			[&exact](VkCommandBuffer cmd){exact.Record(cmd);});
	std::printf("[Porpoise Exact] original Gfx %s; NES emulation remains separate\\n",
		exact_ready?"active":"unavailable, legacy fallback");

	// The key sounds (vk-285-47). Without an audio port the shelf is simply quiet.
	Mixer* mixer = nullptr;
	AudioOut* audio = nullptr;
	if (paths.sound)
	{
		const double s0 = Now();
		std::printf("[frontend] building menu sounds\n");
		mixer = new Mixer();
		mixer->Build();
		audio = new AudioOut();
		if (!audio->Start(mixer))
		{
			delete audio;
			audio = nullptr;
			delete mixer;
			mixer = nullptr;
		}
		std::printf("[frontend] sound %s (%.0f ms)\n", audio ? "on" : "off", (Now() - s0) * 1000.0);
	}
	else
		std::printf("[frontend] sound off (nomenusound)\n");

	App app;
	AppConfig acfg;
    acfg.options.rescan_library=[root=paths.top_dir](std::string& error){return fileio::WriteAtomic(root+"/cache/rescan-library","1\n",error);};
	acfg.build_tag = build_tag ? build_tag : "";
	acfg.test_build = paths.test_build;   // test build 1: the TESTING watermark
	acfg.build_label = paths.build_label;
	acfg.test_note = paths.test_note; // vk-285-105
	acfg.preselect = preselect;
	acfg.sound = mixer;
	acfg.exact = exact_ready ? &exact : nullptr;
	acfg.exact_failed = !exact_ready;
	// vk-285-114: the options sheet (Square) edits the files the settings page does, and logs as it does.
	acfg.options.settings_dir = paths.settings_dir;
	acfg.options.gs_ini = paths.gs_ini;
	acfg.options.patches_dir = paths.patches_dir;
	acfg.options.memcards_dir = paths.memcards_dir;
#ifdef ORBIS_NFS
	const std::string nfs_config = paths.top_dir + "/nfs.txt";
	acfg.options.nfs_url = [nfs_config]() {
		const auto dirs = fe::nfs::ConfiguredDirectories(nfs_config);
		return dirs.empty() ? std::string() : dirs.front();
	};
	acfg.options.save_nfs_url = [nfs_config](const std::string& url, std::string& error) {
		return fe::nfs::SaveConfiguration(nfs_config, url, error);
	};
#endif
	acfg.options.presets.assign(reinterpret_cast<const char*>(fe_presets), static_cast<size_t>(fe_presets_end - fe_presets));
	{
		const std::string log_path = paths.settings_log;
		acfg.options.log = [log_path](const std::string& line) { AppendSettingsLog(log_path, line); };
	}
	acfg.refresh_game = [paths](GameInfo& g) {
		g.badges.clear();
		ReadBadges(g, paths.settings_dir, paths.gs_ini, paths.patches_dir);
	};
	std::printf("[frontend] initializing shelf for %zu games\n",games.size());
	bool ok = app.Init(&renderer, fonts, games, covers, acfg);
	std::printf("[frontend] up in %.0f ms (%s)\n", (Now() - t0) * 1000.0, ok ? "ok" : renderer.error().c_str());
	std::fflush(stdout);
	// vk-285-50: the QR tile's address, looked up again every few seconds in case the IP changes.
	double web_checked = -1e9;
	auto refresh_web = [&](double now) {
		if (!g_web || now - web_checked < 3.0)
			return;
		web_checked = now;
		std::string url, shown;
		g_web->Address(url, shown);
		app.SetWebUrl(url, shown);
	};

	FrameDesc frame;
	double last_t = Now(), report_t = last_t;
	unsigned frames = 0, slot = 0;
	double worst = 0;
	bool first_shown = false;
	snes::ShelfRuntime nes_game(paths.top_dir);
	while (ok)
	{
		const double now = Now();
		refresh_web(now);
		const double dt = now - last_t;
		last_t = now;
		worst = std::max(worst, dt);
		PadData pd;
		std::memset(&pd, 0, sizeof(pd));
		pd.lx = pd.ly = 128;
		Input in;
		if (scePadReadState(pad, &pd) == 0)
		{
			in.left = (pd.buttons & kPadLeft) || pd.lx < 48;
			in.right = (pd.buttons & kPadRight) || pd.lx > 208;
			in.cross = pd.buttons & kPadCross;
			in.options = pd.buttons & kPadOptions;
			in.l1 = pd.buttons & kPadL1;
			in.r1 = pd.buttons & kPadR1;
			in.up = (pd.buttons & kPadUp) || pd.ly < 48; // vk-285-114
			in.down = (pd.buttons & kPadDown) || pd.ly > 208;
			in.square = pd.buttons & kPadSquare;
			in.triangle = pd.buttons & kPadTriangle;
			in.circle = pd.buttons & kPadCircle;
			in.l2 = (pd.buttons & kPadL2) || pd.l2 > 160; // vk-285-116
			in.r2 = (pd.buttons & kPadR2) || pd.r2 > 160;
		}
        // (AI-assisted) The original shelf stays alive on the same Vulkan display.
        if (!renderer.WaitForSlot()) { ok = false; break; }
        if (nes_game.Active())
        {
            uint32_t raw = pd.buttons;
            if (nes_game.UseLeftStickDpad()) { if (pd.lx < 48) raw |= kPadLeft; else if (pd.lx > 208) raw |= kPadRight; }
            if (nes_game.UseLeftStickDpad()) { if (pd.ly < 48) raw |= kPadUp; else if (pd.ly > 208) raw |= kPadDown; }
            if(pd.l2>160)raw|=kPadL2;if(pd.r2>160)raw|=kPadR2;
            if (!nes_game.Build(renderer, frame, raw, now))
            {
                orbis_web_now_playing("");
                if (audio) audio->Start(mixer);
                app.Resume(nes_game.error);
                app.Build(frame, Clock());
            }
        }
        else
        {
            app.Update(dt, in);
            if (app.ExitRequested()) {
                std::printf("[SNESXY] Confirmed close from the home screen\\n");
                break; // Proceed through normal Vulkan/audio/HTTP/pad teardown.
            }
            app.Build(frame, Clock());
            if (app.Done() && app.Chosen() >= 0 && static_cast<size_t>(app.Chosen()) < games.size())
            {
                if (audio) audio->Stop();
                if (!nes_game.Start(games[static_cast<size_t>(app.Chosen())].path, renderer))
                {
                    if (audio) audio->Start(mixer);
                    app.Resume(nes_game.error); app.Build(frame, Clock());
                }
                else orbis_web_now_playing(games[static_cast<size_t>(app.Chosen())].path);
            }
        }
		// The unmodified NES game renderer runs without any menu overlay.
		// Settings sheet is rendered by PS5SNESX after the background pass.
		// Disable the full-screen Porpoise overlay while the sheet is shown,
		// otherwise it hides the panel and Square seems to do nothing.
		renderer.ExactEnabled(exact_ready && !nes_game.Active());
		if (!renderer.WaitForSlot())
		{
			ok = false;
			break;
		}
		uint32_t index = 0;
		VkResult r = display->vk.vkAcquireNextImageKHR(display->device, display->swapchain, UINT64_MAX,
			display->acquired[slot], VK_NULL_HANDLE, &index);
		if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
		{
			std::printf("[frontend] vkAcquireNextImageKHR: %d\n", static_cast<int>(r));
			ok = false;
			break;
		}
		if (!renderer.Render(frame, index, display->acquired[slot], display->rendered[slot]))
		{
			std::printf("[frontend] render: %s\n", renderer.error().c_str());
			ok = false;
			break;
		}
		VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		pi.waitSemaphoreCount = 1;
		pi.pWaitSemaphores = &display->rendered[slot];
		pi.swapchainCount = 1;
		pi.pSwapchains = &display->swapchain;
		pi.pImageIndices = &index;
		r = display->vk.vkQueuePresentKHR(display->queue, &pi);
		if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
		{
			std::printf("[frontend] vkQueuePresentKHR: %d\n", static_cast<int>(r));
			ok = false;
			break;
		}
		slot ^= 1;
		frames++;
		if (!first_shown)
		{
			first_shown = true;
			// vk-285-53: the launch screen (sce_sys/pic1.dds) covers the app until it is hidden; with
			// it, 52 stayed on the launch screen with the shelf running behind it. Hidden once the
			// shelf's first frame is up, so the screen goes from one straight to the other.
			const int hide = orbis_hide_splash();
			std::printf("[frontend] first frame on screen %.0f ms after start; launch screen hidden (%d)\n", (Now() - t0) * 1000.0,
				hide);
			std::fflush(stdout);
		}
		if (now - report_t >= 5.0)
		{
			std::printf("[frontend] %u frames in %.1f s, worst %.1f ms\n", frames, now - report_t, worst * 1000.0);
			std::fflush(stdout);
			frames = 0;
			worst = 0;
			report_t = now;
		}
	}

	if (nes_game.Active()) nes_game.Stop(renderer);
	const int chosen = app.Chosen();
	const bool picked = ok && app.Done();
	renderer.WaitIdle();
	renderer.ExactEnabled(false);
	renderer.SetExactCallbacks({}, {}, {});
	exact.Shutdown();
	app.Shutdown();
	// The worker may be inside a download: stop it from starting another, fail the one in flight,
	// give it a moment, then leave it behind if need be.
	covers->RequestStop();
	g_http.Abort();
	const bool stopped = covers->Stop(1500);
	renderer.Shutdown();
	display->Destroy();
	delete display;
	if (stopped)
	{
		delete covers;
		delete fonts;
		g_http.Term();
	}
	else
		std::printf("[frontend] a cover download is still running; leaving it to finish\n");
	scePadClose(pad);
	if (audio)
	{
		// Let the launch sound ring out: it lasts about 0.8 s, the shelf closes 0.6 s after the press.
		const double s0 = Now();
		while (!mixer->Idle() && Now() - s0 < 0.4)
			usleep(5000);
		audio->Stop();
		delete audio;
		delete mixer;
	}

    if (app.ExitRequested()) {
        std::printf("[SNESXY] All native resources released; exiting\\n");
        std::fflush(stdout);
        ::exit(0); // main_shelf.cpp would otherwise sleep forever.
    }
	if (!picked)
	{
		std::printf("[frontend] stopped without a pick; the plain list takes over\n");
		std::fflush(stdout);
		return {};
	}
	*ran = true;
	const GameInfo& g = games[static_cast<size_t>(chosen)];
	WriteLastGame(paths.top_dir, g.file);
	std::printf("[frontend] picked %s (%s)\n", g.file.c_str(), g.serial.c_str());
	std::fflush(stdout);
	return g.path;
}
