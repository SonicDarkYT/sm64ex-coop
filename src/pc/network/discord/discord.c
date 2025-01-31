#include "discord.h"
#include "user.h"
#include "activity.h"
#include "lobby.h"
#include "discord_network.h"
#include "pc/network/version.h"
#include "pc/djui/djui.h"
#include "pc/logfile.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winuser.h>
#else
#include <unistd.h>
#define MAX_PATH 1024
#endif

#define MAX_LAUNCH_CMD (MAX_PATH + 12)

static int64_t applicationId = 752700005210390568;
struct DiscordApplication app = { 0 };
bool gDiscordInitialized = false;
bool gDiscordFailed = false;

static void discord_sdk_log_callback(UNUSED void* hook_data, enum EDiscordLogLevel level, const char* message) {
    LOGFILE_INFO(LFT_DISCORD, "callback (%d): %s", level, message);
}

void discord_fatal(int rc) {
#if defined(_WIN32) || defined(_WIN64)
    char errorMessage[132] = { 0 };
    snprintf(errorMessage, 132, "Discord threw an error.\r\n\r\nTo fix: \r\n1. Close the game.\r\n2. Restart Discord.\r\n3. Start the game.\r\n\r\nRC: %d", rc);
    fflush(stdout);
    fflush(stderr);
    LOGFILE_ERROR(LFT_DISCORD, "discord fatal %d", rc);
    logfile_close(LFT_DISCORD);
    MessageBox(NULL,
        errorMessage,
        "Fatal Discord Error",
        MB_ICONERROR | MB_OK | MB_DEFBUTTON1
    );
    exit(1);
#else
    SOFT_ASSERT(rc != DiscordResult_ServiceUnavailable);
    SOFT_ASSERT(rc != DiscordResult_InvalidVersion);
    SOFT_ASSERT(rc != DiscordResult_LockFailed);
    SOFT_ASSERT(rc != DiscordResult_InternalError);
    SOFT_ASSERT(rc != DiscordResult_InvalidPayload);
    SOFT_ASSERT(rc != DiscordResult_InvalidCommand);
    SOFT_ASSERT(rc != DiscordResult_InvalidPermissions);
    SOFT_ASSERT(rc != DiscordResult_NotFetched);
    SOFT_ASSERT(rc != DiscordResult_NotFound);
    SOFT_ASSERT(rc != DiscordResult_Conflict);
    SOFT_ASSERT(rc != DiscordResult_InvalidSecret);
    SOFT_ASSERT(rc != DiscordResult_InvalidJoinSecret);
    SOFT_ASSERT(rc != DiscordResult_NoEligibleActivity);
    SOFT_ASSERT(rc != DiscordResult_InvalidInvite);
    SOFT_ASSERT(rc != DiscordResult_NotAuthenticated);
    SOFT_ASSERT(rc != DiscordResult_InvalidAccessToken);
    SOFT_ASSERT(rc != DiscordResult_ApplicationMismatch);
    SOFT_ASSERT(rc != DiscordResult_InvalidDataUrl);
    SOFT_ASSERT(rc != DiscordResult_InvalidBase64);
    SOFT_ASSERT(rc != DiscordResult_NotFiltered);
    SOFT_ASSERT(rc != DiscordResult_LobbyFull);
    SOFT_ASSERT(rc != DiscordResult_InvalidFilename);
    SOFT_ASSERT(rc != DiscordResult_InvalidFileSize);
    SOFT_ASSERT(rc != DiscordResult_InvalidEntitlement);
    SOFT_ASSERT(rc != DiscordResult_NotInstalled);
    SOFT_ASSERT(rc != DiscordResult_NotRunning);
    SOFT_ASSERT(rc != DiscordResult_InsufficientBuffer);
    SOFT_ASSERT(rc != DiscordResult_PurchaseCanceled);
    SOFT_ASSERT(rc != DiscordResult_InvalidGuild);
    SOFT_ASSERT(rc != DiscordResult_InvalidEvent);
    SOFT_ASSERT(rc != DiscordResult_InvalidChannel);
    SOFT_ASSERT(rc != DiscordResult_InvalidOrigin);
    SOFT_ASSERT(rc != DiscordResult_RateLimited);
    SOFT_ASSERT(rc != DiscordResult_OAuth2Error);
    SOFT_ASSERT(rc != DiscordResult_SelectChannelTimeout);
    SOFT_ASSERT(rc != DiscordResult_SelectVoiceForceRequired);
    SOFT_ASSERT(rc != DiscordResult_CaptureShortcutAlreadyListening);
    SOFT_ASSERT(rc != DiscordResult_UnauthorizedForAchievement);
    SOFT_ASSERT(rc != DiscordResult_InvalidGiftCode);
    SOFT_ASSERT(rc != DiscordResult_PurchaseError);
    SOFT_ASSERT(rc != DiscordResult_TransactionAborted);
    SOFT_ASSERT(rc == DiscordResult_Ok);
#endif
}

static void set_instance_env_variable(void) {
    // set local instance id
    char environmentVariables[64] = { 0 };
    int instance = (gCLIOpts.Discord == 0) ? 0 : (gCLIOpts.Discord - 1);
    sprintf(environmentVariables, "DISCORD_INSTANCE_ID=%d", instance);
    putenv(environmentVariables);
    LOGFILE_INFO(LFT_DISCORD, "set environment variables: %s", environmentVariables);
}

static void get_oauth2_token_callback(UNUSED void* data, enum EDiscordResult result, struct DiscordOAuth2Token* token) {
    LOGFILE_INFO(LFT_DISCORD, "> get_oauth2_token_callback returned %d", result);
    if (result != DiscordResult_Ok) { return; }
    LOGFILE_INFO(LFT_DISCORD, "OAuth2 token: %s", token->access_token);
}

static void register_launch_command(void) {
    char cmd[MAX_LAUNCH_CMD];
    int rc;
#if defined(_WIN32) || defined(_WIN64)
    HMODULE hModule = GetModuleHandle(NULL);
    if (hModule == NULL) {
        LOGFILE_ERROR(LFT_DISCORD, "unable to retrieve absolute path!");
        return;
    }
    GetModuleFileName(hModule, cmd, sizeof(cmd));
#else
    char path[MAX_LAUNCH_CMD] = { 0 };
    snprintf(path, MAX_LAUNCH_CMD - 1, "/proc/%d/exe", getpid());
    rc = readlink(path, cmd, MAX_LAUNCH_CMD - 1);
    if (rc <= 0) {
        LOGFILE_ERROR(LFT_DISCORD, "unable to retrieve absolute path! rc = %d", rc);
        return;
    }
#endif
    strncat(cmd, " --discord 1", MAX_LAUNCH_CMD - 1);
    rc = app.activities->register_command(app.activities, cmd);
    if (rc != DiscordResult_Ok) {
        LOGFILE_ERROR(LFT_DISCORD, "register command failed %d", rc);
        return;
    }
    LOGFILE_INFO(LFT_DISCORD, "cmd: %s", cmd);
}

static void* ns_discord_dup_addr(u8 localIndex) {
    void* address = malloc(sizeof(DiscordUserId));
    memcpy(address, &gNetworkUserIds[localIndex], sizeof(DiscordUserId));
    return address;
}

static bool ns_discord_match_addr(void* addr1, void* addr2) {
    return !memcmp(addr1, addr2, sizeof(u64));
}

static void ns_discord_update(void) {
    if (!gDiscordInitialized) { return; }
    discord_lobby_update();
    DISCORD_REQUIRE(app.core->run_callbacks(app.core));
    discord_network_flush();
}

static bool ns_discord_initialize(enum NetworkType networkType) {
#ifdef DEBUG
    set_instance_env_variable();
#endif

    if (app.core != NULL) {
        app.core->set_log_hook(app.core, DiscordLogLevel_Debug, NULL, discord_sdk_log_callback);
    }

    if (!gDiscordInitialized) {
        // set up discord params
        struct DiscordCreateParams params;
        DiscordCreateParamsSetDefault(&params);
        params.client_id = applicationId;
        params.flags = DiscordCreateFlags_NoRequireDiscord;
        params.event_data = &app;
        params.user_events = discord_user_initialize();
        params.activity_events = discord_activity_initialize();
        params.lobby_events = discord_lobby_initialize();

        gCurLobbyId = 0;
        gLobbyCreateRetry = false;
        gLobbyCreateAttempts = 0;
        gLobbyCreateAttemptElapsed = 0;

        int rc = DiscordCreate(DISCORD_VERSION, &params, &app.core);
        if (app.core != NULL) {
            app.core->set_log_hook(app.core, DiscordLogLevel_Debug, NULL, discord_sdk_log_callback);
        }
        gDiscordFailed = false;
        if (networkType != NT_NONE) {
            DISCORD_REQUIRE(rc);
        } else if (rc) {
            LOGFILE_ERROR(LFT_DISCORD, "DiscordCreate failed: %d", rc);
            djui_popup_create("\\#ffa0a0\\Error:\\#c8c8c8\\ Could not detect Discord.\n\\#a0a0a0\\Try closing the game, restarting Discord, and opening the game again.", 3);
            gDiscordFailed = true;
            return false;
        }

        // set up manager pointers
        app.users = app.core->get_user_manager(app.core);
        app.achievements = app.core->get_achievement_manager(app.core);
        app.activities = app.core->get_activity_manager(app.core);
        app.application = app.core->get_application_manager(app.core);
        app.lobbies = app.core->get_lobby_manager(app.core);

        // register launch params
        register_launch_command();

        // get oath2 token
        app.application->get_oauth2_token(app.application, NULL, get_oauth2_token_callback);

        // set activity
        discord_activity_update(false);
    }

    // create lobby
    if (networkType == NT_SERVER) { discord_lobby_create(); }

    gActivityLock = false;
    gDiscordInitialized = true;
    LOGFILE_INFO(LFT_DISCORD, "initialized");

    return true;
}

static void ns_discord_shutdown(void) {
    if (!gDiscordInitialized) { return; }
    discord_lobby_leave();
    gActivityLock = false;
    LOGFILE_INFO(LFT_DISCORD, "shutdown");
}

struct NetworkSystem gNetworkSystemDiscord = {
    .initialize = ns_discord_initialize,
    .get_id     = ns_discord_get_id,
    .get_id_str = ns_discord_get_id_str,
    .save_id    = ns_discord_save_id,
    .clear_id   = ns_discord_clear_id,
    .dup_addr   = ns_discord_dup_addr,
    .match_addr = ns_discord_match_addr,
    .update     = ns_discord_update,
    .send       = ns_discord_network_send,
    .shutdown   = ns_discord_shutdown,
    .requireServerBroadcast = false,
    .name       = "Discord",
};
