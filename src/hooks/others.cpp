/*
 * others.cpp
 *
 *  Created on: Jan 8, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "ucccccp/ucccccp.hpp"
#include "hack.hpp"
#include "hitrate.hpp"
#include "chatlog.hpp"
#include "netmessage.hpp"
#include <boost/algorithm/string.hpp>

#if ENABLE_VISUALS == 1

static CatVar medal_flip(CV_SWITCH, "medal_flip", "0", "Infinite Medal Flip",
                         "");

// This hook isn't used yet!
/*int C_TFPlayer__DrawModel_hook(IClientEntity *_this, int flags)
{
    float old_invis = *(float *) ((uintptr_t) _this + 79u);
    if (no_invisibility)
    {
        if (old_invis < 1.0f)
        {
            *(float *) ((uintptr_t) _this + 79u) = 0.5f;
        }
    }

    *(float *) ((uintptr_t) _this + 79u) = old_invis;
}*/

static CatVar no_arms(CV_SWITCH, "no_arms", "0", "No Arms",
                      "Removes arms from first person");
static CatVar no_hats(CV_SWITCH, "no_hats", "0", "No Hats",
                      "Removes non-stock hats");
float last_say = 0.0f;
void DrawModelExecute_hook(IVModelRender *_this, const DrawModelState_t &state,
                           const ModelRenderInfo_t &info, matrix3x4_t *matrix)
{
    static const DrawModelExecute_t original =
        (DrawModelExecute_t) hooks::modelrender.GetMethod(
            offsets::DrawModelExecute());
    static const char *name;
    static std::string sname;
    static IClientUnknown *unk;
    static IClientEntity *ent;

    if (!cathook ||
        !(spectator_target || no_arms || no_hats ||
          (clean_screenshots && g_IEngine->IsTakingScreenshot())))
    {
        original(_this, state, info, matrix);
        return;
    }

    PROF_SECTION(DrawModelExecute);

    if (no_arms || no_hats)
    {
        if (info.pModel)
        {
            name = g_IModelInfo->GetModelName(info.pModel);
            if (name)
            {
                sname = name;
                if (no_arms && sname.find("arms") != std::string::npos)
                {
                    return;
                }
                else if (no_hats &&
                         sname.find("player/items") != std::string::npos)
                {
                    return;
                }
            }
        }
    }

    unk = info.pRenderable->GetIClientUnknown();
    if (unk)
    {
        ent = unk->GetIClientEntity();
        if (ent)
        {
            if (ent->entindex() == spectator_target)
            {
                return;
            }
        }
        if (ent && !effect_chams::g_EffectChams.drawing &&
            effect_chams::g_EffectChams.ShouldRenderChams(ent))
        {
            return;
        }
    }

    original(_this, state, info, matrix);
}

int IN_KeyEvent_hook(void *_this, int eventcode, int keynum,
                     const char *pszCurrentBinding)
{
    static const IN_KeyEvent_t original =
        (IN_KeyEvent_t) hooks::client.GetMethod(offsets::IN_KeyEvent());
#if ENABLE_GUI
/*
if (g_pGUI->ConsumesKey((ButtonCode_t)keynum) && g_pGUI->Visible()) {
    return 0;
}
*/
#endif
    return original(_this, eventcode, keynum, pszCurrentBinding);
}

CatVar override_fov_zoomed(CV_FLOAT, "fov_zoomed", "0", "FOV override (zoomed)",
                           "Overrides FOV with this value when zoomed in "
                           "(default FOV when zoomed is 20)");
CatVar override_fov(CV_FLOAT, "fov", "0", "FOV override",
                    "Overrides FOV with this value");

CatVar freecam(CV_KEY, "debug_freecam", "0", "Freecam");
int spectator_target{ 0 };

CatCommand spectate("spectate", "Spectate", [](const CCommand &args) {
    if (args.ArgC() < 1)
    {
        spectator_target = 0;
        return;
    }
    int id = atoi(args.Arg(1));
    if (!id)
        spectator_target = 0;
    else
    {
        spectator_target = g_IEngine->GetPlayerForUserID(id);
    }
});

void OverrideView_hook(void *_this, CViewSetup *setup)
{
    static const OverrideView_t original =
        (OverrideView_t) hooks::clientmode.GetMethod(offsets::OverrideView());
    static bool zoomed;
    original(_this, setup);
    if (!cathook)
        return;
    if (g_pLocalPlayer->bZoomed && override_fov_zoomed)
    {
        setup->fov = override_fov_zoomed;
    }
    else
    {
        if (override_fov)
        {
            setup->fov = override_fov;
        }
    }

    if (spectator_target)
    {
        CachedEntity *spec = ENTITY(spectator_target);
        if (CE_GOOD(spec) && !CE_BYTE(spec, netvar.iLifeState))
        {
            setup->origin =
                spec->m_vecOrigin + CE_VECTOR(spec, netvar.vViewOffset);
            // why not spectate yourself
            if (spec == LOCAL_E)
            {
                setup->angles =
                    CE_VAR(spec, netvar.m_angEyeAnglesLocal, QAngle);
            }
            else
            {
                setup->angles = CE_VAR(spec, netvar.m_angEyeAngles, QAngle);
            }
        }
        if (g_IInputSystem->IsButtonDown(ButtonCode_t::KEY_SPACE))
        {
            spectator_target = 0;
        }
    }

    if (freecam)
    {
        static Vector freecam_origin{ 0 };
        static bool freecam_last{ false };
        if (freecam.KeyDown())
        {
            if (not freecam_last)
            {
                freecam_origin = setup->origin;
            }
            float sp, sy, cp, cy;
            QAngle angle;
            Vector forward;
            g_IEngine->GetViewAngles(angle);
            sy        = sinf(DEG2RAD(angle[1]));
            cy        = cosf(DEG2RAD(angle[1]));
            sp        = sinf(DEG2RAD(angle[0]));
            cp        = cosf(DEG2RAD(angle[0]));
            forward.x = cp * cy;
            forward.y = cp * sy;
            forward.z = -sp;
            forward *= 4;
            freecam_origin += forward;
            setup->origin = freecam_origin;
        }
        freecam_last = freecam.KeyDown();
    }

    draw::fov = setup->fov;
}

#endif

bool CanPacket_hook(void *_this)
{
    const CanPacket_t original =
        (CanPacket_t) hooks::netchannel.GetMethod(offsets::CanPacket());
    return *bSendPackets && original(_this);
}

CUserCmd *GetUserCmd_hook(IInput *_this, int sequence_number)
{
    static const GetUserCmd_t original =
        (GetUserCmd_t) hooks::input.GetMethod(offsets::GetUserCmd());
    static CUserCmd *def;
    static int oldcmd;
    static INetChannel *ch;

    def = original(_this, sequence_number);
    if (def &&
        command_number_mod.find(def->command_number) !=
            command_number_mod.end())
    {
        // logging::Info("Replacing command %i with %i", def->command_number,
        // command_number_mod[def->command_number]);
        oldcmd              = def->command_number;
        def->command_number = command_number_mod[def->command_number];
        def->random_seed =
            MD5_PseudoRandom(unsigned(def->command_number)) & 0x7fffffff;
        command_number_mod.erase(command_number_mod.find(oldcmd));
        *(int *) ((unsigned) g_IBaseClientState +
                  offsets::lastoutgoingcommand()) = def->command_number - 1;
        ch =
            (INetChannel *) g_IEngine
                ->GetNetChannelInfo(); //*(INetChannel**)((unsigned)g_IBaseClientState
                                       //+ offsets::m_NetChannel());
        *(int *) ((unsigned) ch + offsets::m_nOutSequenceNr()) =
            def->command_number - 1;
    }
    return def;
}

static CatVar log_sent(CV_SWITCH, "debug_log_sent_messages", "0",
                       "Log sent messages");

static CatCommand plus_use_action_slot_item_server(
    "+cat_use_action_slot_item_server", "use_action_slot_item_server", []() {
        KeyValues *kv = new KeyValues("+use_action_slot_item_server");
        g_pLocalPlayer->using_action_slot_item = true;
        g_IEngine->ServerCmdKeyValues(kv);
    });

static CatCommand minus_use_action_slot_item_server(
    "-cat_use_action_slot_item_server", "use_action_slot_item_server", []() {
        KeyValues *kv = new KeyValues("-use_action_slot_item_server");
        g_pLocalPlayer->using_action_slot_item = false;
        g_IEngine->ServerCmdKeyValues(kv);
    });

static CatVar newlines_msg(CV_INT, "chat_newlines", "0", "Prefix newlines",
                           "Add # newlines before each your message", 0, 24);
// TODO replace \\n with \n
// TODO name \\n = \n
// static CatVar queue_messages(CV_SWITCH, "chat_queue", "0", "Queue messages",
// "Use this if you want to use spam/killsay and still be able to chat normally
// (without having your msgs eaten by valve cooldown)");

static CatVar airstuck(CV_KEY, "airstuck", "0", "Airstuck", "");
static CatVar crypt_chat(
    CV_SWITCH, "chat_crypto", "1", "Crypto chat",
    "Start message with !! and it will be only visible to cathook users");
static CatVar chat_filter(CV_STRING, "chat_censor", "", "Censor words",
                          "Spam Chat with newlines if the chosen words are "
                          "said, seperate with commas");
static CatVar chat_filter_enabled(CV_SWITCH, "chat_censor_enabled", "0",
                                  "Enable censor", "Censor Words in chat");
static CatVar server_crash_key(CV_KEY, "crash_server", "0", "Server crash key",
                               "hold key and wait...");
bool SendNetMsg_hook(void *_this, INetMessage &msg, bool bForceReliable = false,
                     bool bVoice = false)
{
    static size_t say_idx, say_team_idx;
    static int offset;
    static std::string newlines;
    static NET_StringCmd stringcmd;

    // This is a INetChannel hook - it SHOULDN'T be static because netchannel
    // changes.
    const SendNetMsg_t original =
        (SendNetMsg_t) hooks::netchannel.GetMethod(offsets::SendNetMsg());
    // net_StringCmd
    if (msg.GetType() == 4 && (newlines_msg || crypt_chat))
    {
        std::string str(msg.ToString());
        say_idx      = str.find("net_StringCmd: \"say \"");
        say_team_idx = str.find("net_StringCmd: \"say_team \"");
        if (!say_idx || !say_team_idx)
        {
            offset    = say_idx ? 26 : 21;
            bool crpt = false;
            if (crypt_chat)
            {
                std::string msg(str.substr(offset));
                msg = msg.substr(0, msg.length() - 2);
                if (msg.find("!!") == 0)
                {
                    msg  = ucccccp::encrypt(msg.substr(2));
                    str  = str.substr(0, offset) + msg + "\"\"";
                    crpt = true;
                }
            }
            if (!crpt && newlines_msg)
            {
                // TODO move out? update in a value change callback?
                newlines = std::string((int) newlines_msg, '\n');
                str.insert(offset, newlines);
            }
            str = str.substr(16, str.length() - 17);
            // if (queue_messages && !chat_stack::CanSend()) {
            stringcmd.m_szCommand = str.c_str();
            return original(_this, stringcmd, bForceReliable, bVoice);
            //}
        }
    }
    static ConVar *sv_player_usercommand_timeout =
        g_ICvar->FindVar("sv_player_usercommand_timeout");
    static float lastcmd = 0.0f;
    if (lastcmd > g_GlobalVars->absoluteframetime)
    {
        lastcmd = g_GlobalVars->absoluteframetime;
    }
    if (airstuck.KeyDown() && !g_Settings.bInvalid)
    {
        if (CE_GOOD(LOCAL_E))
        {
            if (lastcmd + sv_player_usercommand_timeout->GetFloat() - 0.1f <
                g_GlobalVars->curtime)
            {
                if (msg.GetType() == clc_Move)
                    return false;
            }
            else
            {
                lastcmd = g_GlobalVars->absoluteframetime;
            }
        }
    }
    if (log_sent && msg.GetType() != 3 && msg.GetType() != 9)
    {
        logging::Info("=> %s [%i] %s", msg.GetName(), msg.GetType(),
                      msg.ToString());
        unsigned char buf[4096];
        bf_write buffer("cathook_debug_buffer", buf, 4096);
        logging::Info("Writing %i", msg.WriteToBuffer(buffer));
        std::string bytes    = "";
        constexpr char h2c[] = "0123456789abcdef";
        for (int i = 0; i < buffer.GetNumBytesWritten(); i++)
        {
            // bytes += format(h2c[(buf[i] & 0xF0) >> 4], h2c[(buf[i] & 0xF)], '
            // ');
            bytes += format((unsigned short) buf[i], ' ');
        }
        logging::Info("%i bytes => %s", buffer.GetNumBytesWritten(),
                      bytes.c_str());
    }
    return original(_this, msg, bForceReliable, bVoice);
}

static CatVar die_if_vac(CV_SWITCH, "die_if_vac", "0", "Die if VAC banned");

void Shutdown_hook(void *_this, const char *reason)
{
    g_Settings.bInvalid = true;
    // This is a INetChannel hook - it SHOULDN'T be static because netchannel
    // changes.
    const Shutdown_t original =
        (Shutdown_t) hooks::netchannel.GetMethod(offsets::Shutdown());
    logging::Info("Disconnect: %s", reason);
    if (strstr(reason, "banned"))
    {
        if (die_if_vac)
        {
            logging::Info("VAC banned");
            *(int *) 0 = 0;
            exit(1);
        }
    }
#if ENABLE_IPC
    ipc::UpdateServerAddress(true);
#endif
    if (cathook && (disconnect_reason.convar_parent->m_StringLength > 3) &&
        strstr(reason, "user"))
    {
        original(_this, disconnect_reason_newlined);
    }
    else
    {
        original(_this, reason);
    }

    if (hacks::shared::autojoin::auto_queue)
        tfmm::queue_start();
}

static CatVar resolver(CV_SWITCH, "resolver", "0", "Resolve angles");

CatEnum namesteal_enum({ "OFF", "PASSIVE", "ACTIVE" });
CatVar namesteal(namesteal_enum, "name_stealer", "0", "Name Stealer",
                 "Attemt to steal your teammates names. Usefull for avoiding "
                 "kicks\nPassive only changes when the name stolen is no "
                 "longer the best name to use\nActive Attemps to change the "
                 "name whenever possible");

static std::string stolen_name;

// Func to get a new entity to steal name from and returns true if a target has
// been found
bool StolenName()
{

    // Array to store potential namestealer targets with a bookkeeper to tell
    // how full it is
    int potential_targets[32];
    int potential_targets_length = 0;

    // Go through entities looking for potential targets
    for (int i = 1; i < HIGHEST_ENTITY; i++)
    {
        CachedEntity *ent = ENTITY(i);

        // Check if ent is a good target
        if (!ent)
            continue;
        if (ent == LOCAL_E)
            continue;
        if (!ent->m_Type == ENTITY_PLAYER)
            continue;
        if (ent->m_bEnemy)
            continue;

        // Check if name is current one
        player_info_s info;
        if (g_IEngine->GetPlayerInfo(ent->m_IDX, &info))
        {

            // If our name is the same as current, than change it
            if (std::string(info.name) == stolen_name)
            {
                // Since we found the ent we stole our name from and it is still
                // good, if user settings are passive, then we return true and
                // dont alter our name
                if ((int) namesteal == 1)
                {
                    return true;
                    // Otherwise we continue to change our name to something
                    // else
                }
                else
                    continue;
            }

            // a ent without a name is no ent we need, contine for a different
            // one
        }
        else
            continue;

        // Save the ent to our array
        potential_targets[potential_targets_length] = i;
        potential_targets_length++;

        // With our maximum amount of players reached, dont search for anymore
        if (potential_targets_length >= 32)
            break;
    }

    // Checks to prevent crashes
    if (potential_targets_length == 0)
        return false;

    // Get random number that we can use with our array
    int target_random_num =
        floor(RandFloatRange(0, potential_targets_length - 0.1F));

    // Get a idx from our random array position
    int new_target = potential_targets[target_random_num];

    // Grab username of user
    player_info_s info;
    if (g_IEngine->GetPlayerInfo(new_target, &info))
    {

        // If our name is the same as current, than change it and return true
        stolen_name = std::string(info.name);
        return true;
    }

    // Didnt get playerinfo
    return false;
}

static CatVar ipc_name(CV_STRING, "name_ipc", "", "IPC Name");

const char *GetFriendPersonaName_hook(ISteamFriends *_this, CSteamID steamID)
{
    static const GetFriendPersonaName_t original =
        (GetFriendPersonaName_t) hooks::steamfriends.GetMethod(
            offsets::GetFriendPersonaName());

#if ENABLE_IPC
    if (ipc::peer)
    {
        static std::string namestr(ipc_name.GetString());
        namestr.assign(ipc_name.GetString());
        if (namestr.length() > 3)
        {
            ReplaceString(namestr, "%%", std::to_string(ipc::peer->client_id));
            return namestr.c_str();
        }
    }
#endif

    // Check User settings if namesteal is allowed
    if (namesteal && steamID == g_ISteamUser->GetSteamID())
    {

        // We dont want to steal names while not in-game as there are no targets
        // to steal from. We want to be on a team as well to get teammates names
        if (g_IEngine->IsInGame() && g_pLocalPlayer->team)
        {

            // Check if we have a username to steal, func automaticly steals a
            // name in it.
            if (StolenName())
            {

                // Return the name that has changed from the func above
                return format(stolen_name, "\x0F").c_str();
            }
        }
    }

    if ((strlen(force_name.GetString()) > 1) &&
        steamID == g_ISteamUser->GetSteamID())
    {

        return force_name_newlined;
    }
    return original(_this, steamID);
}

void FireGameEvent_hook(void *_this, IGameEvent *event)
{
    static const FireGameEvent_t original =
        (FireGameEvent_t) hooks::clientmode4.GetMethod(
            offsets::FireGameEvent());
    const char *name = event->GetName();
    if (name)
    {
        if (event_log)
        {
            if (!strcmp(name, "player_connect_client") ||
                !strcmp(name, "player_disconnect") ||
                !strcmp(name, "player_team"))
            {
                return;
            }
        }
        //		hacks::tf2::killstreak::fire_event(event);
    }
    original(_this, event);
}
CatVar nightmode(CV_SWITCH, "nightmode", "0", "Enable nightmode", "");
#if ENABLE_VISUALS == 1
void FrameStageNotify_hook(void *_this, int stage)
{
    if (nightmode)
    {
        static int OldNightmode = 0;
        if (OldNightmode != (int) nightmode)
        {

            static ConVar *r_DrawSpecificStaticProp =
                g_ICvar->FindVar("r_DrawSpecificStaticProp");
            if (!r_DrawSpecificStaticProp)
            {
                r_DrawSpecificStaticProp =
                    g_ICvar->FindVar("r_DrawSpecificStaticProp");
                return;
            }
            r_DrawSpecificStaticProp->SetValue(0);

            for (MaterialHandle_t i = g_IMaterialSystem->FirstMaterial();
                 i != g_IMaterialSystem->InvalidMaterial();
                 i = g_IMaterialSystem->NextMaterial(i))
            {
                IMaterial *pMaterial = g_IMaterialSystem->GetMaterial(i);

                if (!pMaterial)
                    continue;
                if (strstr(pMaterial->GetTextureGroupName(), "World") ||
                    strstr(pMaterial->GetTextureGroupName(), "StaticProp"))
                {
                    if (nightmode)
                        if (strstr(pMaterial->GetTextureGroupName(),
                                   "StaticProp"))
                            pMaterial->ColorModulate(0.3f, 0.3f, 0.3f);
                        else
                            pMaterial->ColorModulate(0.05f, 0.05f, 0.05f);
                    else
                        pMaterial->ColorModulate(1.0f, 1.0f, 1.0f);
                }
            }
            OldNightmode = nightmode;
        }
    }
    static IClientEntity *ent;

    PROF_SECTION(FrameStageNotify_TOTAL);

    static const FrameStageNotify_t original =
        (FrameStageNotify_t) hooks::client.GetMethod(
            offsets::FrameStageNotify());

    if (!g_IEngine->IsInGame())
        g_Settings.bInvalid = true;
    {
        PROF_SECTION(FSN_skinchanger);
        hacks::tf2::skinchanger::FrameStageNotify(stage);
    }
    if (resolver && cathook && !g_Settings.bInvalid &&
        stage == FRAME_NET_UPDATE_POSTDATAUPDATE_START)
    {
        PROF_SECTION(FSN_resolver);
        for (int i = 1; i < 32 && i < HIGHEST_ENTITY; i++)
        {
            if (i == g_IEngine->GetLocalPlayer())
                continue;
            ent = g_IEntityList->GetClientEntity(i);
            if (ent && !ent->IsDormant() && !NET_BYTE(ent, netvar.iLifeState))
            {
                Vector &angles = NET_VECTOR(ent, netvar.m_angEyeAngles);
                if (angles.x >= 90)
                    angles.x = -89;
                if (angles.x <= -90)
                    angles.x = 89;
                angles.y     = fmod(angles.y + 180.0f, 360.0f);
                if (angles.y < 0)
                    angles.y += 360.0f;
                angles.y -= 180.0f;
            }
        }
    }
    if (cathook && stage == FRAME_RENDER_START)
    {
        INetChannel *ch;
        ch = (INetChannel *) g_IEngine->GetNetChannelInfo();
        if (ch && !hooks::IsHooked((void *) ch))
        {
            hooks::netchannel.Set(ch);
            hooks::netchannel.HookMethod((void *) CanPacket_hook,
                                         offsets::CanPacket());
            hooks::netchannel.HookMethod((void *) SendNetMsg_hook,
                                         offsets::SendNetMsg());
            hooks::netchannel.HookMethod((void *) Shutdown_hook,
                                         offsets::Shutdown());
            hooks::netchannel.Apply();
#if ENABLE_IPC
            ipc::UpdateServerAddress();
#endif
        }
    }
    if (cathook && !g_Settings.bInvalid && stage == FRAME_RENDER_START)
    {
        IF_GAME(IsTF())
        {
            if (CE_GOOD(LOCAL_E) && no_zoom)
                RemoveCondition<TFCond_Zoomed>(LOCAL_E);
        }
        if (force_thirdperson && !g_pLocalPlayer->life_state &&
            CE_GOOD(g_pLocalPlayer->entity))
        {
            CE_INT(g_pLocalPlayer->entity, netvar.nForceTauntCam) = 1;
        }
        if (stage == 5 && show_antiaim && g_IInput->CAM_IsThirdPerson())
        {
            if (CE_GOOD(g_pLocalPlayer->entity))
            {
                CE_FLOAT(g_pLocalPlayer->entity, netvar.deadflag + 4) =
                    g_Settings.last_angles.x;
                CE_FLOAT(g_pLocalPlayer->entity, netvar.deadflag + 8) =
                    g_Settings.last_angles.y;
            }
        }
    }
    original(_this, stage);
}
#endif /* TEXTMODE */

static CatVar clean_chat(CV_SWITCH, "clean_chat", "0", "Clean chat",
                         "Removes newlines from chat");
static CatVar dispatch_log(CV_SWITCH, "debug_log_usermessages", "0",
                           "Log dispatched user messages");
std::string clear = "";
Timer sendmsg{};
Timer gitgud{};
std::string lastfilter{};
std::string lastname{};
static bool retrun = false;
bool DispatchUserMessage_hook(void *_this, int type, bf_read &buf)
{
    if (retrun && gitgud.test_and_set(10000))
    {
        PrintChat("\x07%06X%s\x01: %s", 0xe05938, lastname.c_str(),
                  lastfilter.c_str());
        retrun = false;
    }
    int loop_index, s, i, j;
    char *data, c;

    static const DispatchUserMessage_t original =
        (DispatchUserMessage_t) hooks::client.GetMethod(
            offsets::DispatchUserMessage());
    if (type == 4)
    {
        loop_index = 0;
        s          = buf.GetNumBytesLeft();
        if (s < 256)
        {
            data = (char *) alloca(s);
            for (i      = 0; i < s; i++)
                data[i] = buf.ReadByte();
            j           = 0;
            std::string name;
            std::string message;
            for (i = 0; i < 3; i++)
            {
                while ((c = data[j++]) && (loop_index < 128))
                {
                    loop_index++;
                    if (clean_chat)
                        if ((c == '\n' || c == '\r') && (i == 1 || i == 2))
                            data[j - 1] = '*';
                    if (i == 1)
                        name.push_back(c);
                    if (i == 2)
                        message.push_back(c);
                }
            }
            if (chat_filter_enabled && data[0] != LOCAL_E->m_IDX)
            {
                if (!strcmp(chat_filter.GetString(), ""))
                {
                    std::string tmp  = {};
                    std::string tmp2 = {};
                    int iii          = 0;
                    player_info_s info;
                    g_IEngine->GetPlayerInfo(LOCAL_E->m_IDX, &info);
                    std::string name1 = info.name;
                    std::vector<std::string> name2{};
                    std::vector<std::string> name3{};
                    std::string claz = {};
                    switch (g_pLocalPlayer->clazz)
                    {
                    case tf_scout:
                        claz = "scout";
                        break;
                    case tf_soldier:
                        claz = "soldier";
                        break;
                    case tf_pyro:
                        claz = "pyro";
                        break;
                    case tf_demoman:
                        claz = "demo";
                        break;
                    case tf_engineer:
                        claz = "engi";
                        break;
                    case tf_heavy:
                        claz = "heavy";
                        break;
                    case tf_medic:
                        claz = "med";
                        break;
                    case tf_sniper:
                        claz = "sniper";
                        break;
                    case tf_spy:
                        claz = "spy";
                        break;
                    default:
                        break;
                    }
                    for (char i : name1)
                    {
                        if (iii == 2)
                        {
                            iii = 0;
                            tmp += i;
                            name2.push_back(tmp);
                            tmp = "";
                        }
                        else if (iii < 2)
                        {
                            iii++;
                            tmp += i;
                        }
                    }
                    iii = 0;
                    for (char i : name1)
                    {
                        if (iii == 3)
                        {
                            iii = 0;
                            tmp += i;
                            name3.push_back(tmp2);
                            tmp2 = "";
                        }
                        else if (iii < 3)
                        {
                            iii++;
                            tmp2 += i;
                        }
                    }
                    if (tmp.size() > 2)
                        name2.push_back(tmp);
                    if (tmp2.size() > 2)
                        name3.push_back(tmp2);
                    iii                          = 0;
                    std::vector<std::string> res = {
                        "skid", "script", "cheat", "hak",   "hac",  "f1",
                        "hax",  "vac",    "ban",   "lmao",  "bot",  "report",
                        "cat",  "insta",  "revv",  "brass", "kick", claz
                    };
                    for (auto i : name2)
                    {
                        boost::to_lower(i);
                        res.push_back(i);
                    }
                    for (auto i : name3)
                    {
                        boost::to_lower(i);
                        res.push_back(i);
                    }
                    std::string message2 = message;
                    boost::to_lower(message2);
                    boost::replace_all(message2, "4", "a");
                    boost::replace_all(message2, "3", "e");
                    boost::replace_all(message2, "0", "o");
                    boost::replace_all(message2, "6", "g");
                    boost::replace_all(message2, "5", "s");
                    boost::replace_all(message2, "7", "t");
                    for (auto filter : res)
                    {
                        if (retrun)
                            break;
                        if (boost::contains(message2, filter))
                        {

                            if (clear == "")
                            {
                                for (int i = 0; i < 120; i++)
                                    clear += "\n";
                            }
                            *bSendPackets = true;
                            chat_stack::Say(". " + clear, true);
                            retrun     = true;
                            lastfilter = format(filter);
                            lastname   = format(name);
                        }
                    }
                }
                else if (data[0] != LOCAL_E->m_IDX)
                {
                    std::string input = chat_filter.GetString();
                    boost::to_lower(input);
                    std::string message2 = message;
                    std::vector<std::string> result{};
                    boost::split(result, input, boost::is_any_of(","));
                    boost::replace_all(message2, "4", "a");
                    boost::replace_all(message2, "3", "e");
                    boost::replace_all(message2, "0", "o");
                    boost::replace_all(message2, "6", "g");
                    boost::replace_all(message2, "5", "s");
                    boost::replace_all(message2, "7", "t");
                    for (auto filter : result)
                    {
                        if (retrun)
                            break;
                        if (boost::contains(message2, filter))
                        {
                            if (clear == "")
                            {
                                clear = "";
                                for (int i = 0; i < 120; i++)
                                    clear += "\n";
                            }
                            *bSendPackets = true;
                            chat_stack::Say(". " + clear, true);
                            retrun     = true;
                            lastfilter = format(filter);
                            lastname   = format(name);
                        }
                    }
                }
            }
            if (sendmsg.test_and_set(300000) &&
                hacks::shared::antiaim::communicate)
                chat_stack::Say("!!meow");
            if (crypt_chat)
            {
                if (message.find("!!") == 0)
                {
                    if (ucccccp::validate(message))
                    {
                        if (ucccccp::decrypt(message) == "meow" &&
                            hacks::shared::antiaim::communicate &&
                            data[0] != LOCAL_E->m_IDX &&
                            playerlist::AccessData(ENTITY(data[0])).state !=
                                playerlist::k_EState::CAT)
                        {
                            playerlist::AccessData(ENTITY(data[0])).state =
                                playerlist::k_EState::CAT;
                            chat_stack::Say("!!meow");
                        }
                        PrintChat("\x07%06X%s\x01: %s", 0xe05938, name.c_str(),
                                  ucccccp::decrypt(message).c_str());
                    }
                }
            }
            chatlog::LogMessage(data[0], message);
            buf = bf_read(data, s);
            buf.Seek(0);
        }
    }
    if (dispatch_log)
    {
        logging::Info("D> %i", type);
        std::ostringstream str{};
        while (buf.GetNumBytesLeft())
        {
            unsigned char byte = buf.ReadByte();
            str << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(byte) << ' ';
        }
        logging::Info("MESSAGE %d, DATA = [ %s ]", type, str.str().c_str());
        buf.Seek(0);
    }
    votelogger::user_message(buf, type);
    return original(_this, type, buf);
}
const char *skynum[] = { "sky_tf2_04",
                         "sky_upward",
                         "sky_dustbowl_01",
                         "sky_goldrush_01",
                         "sky_granary_01",
                         "sky_well_01",
                         "sky_gravel_01",
                         "sky_badlands_01",
                         "sky_hydro_01",
                         "sky_night_01",
                         "sky_nightfall_01",
                         "sky_trainyard_01",
                         "sky_stormfront_01",
                         "sky_morningsnow_01",
                         "sky_alpinestorm_01",
                         "sky_harvest_01",
                         "sky_harvest_night_01",
                         "sky_halloween",
                         "sky_halloween_night_01",
                         "sky_halloween_night2014_01",
                         "sky_island_01",
                         "sky_jungle_01",
                         "sky_invasion2fort_01",
                         "sky_well_02",
                         "sky_outpost_01",
                         "sky_coastal_01",
                         "sky_rainbow_01",
                         "sky_badlands_pyroland_01",
                         "sky_pyroland_01",
                         "sky_pyroland_02",
                         "sky_pyroland_03" };
CatEnum skys({ "sky_tf2_04",
               "sky_upward",
               "sky_dustbowl_01",
               "sky_goldrush_01",
               "sky_granary_01",
               "sky_well_01",
               "sky_gravel_01",
               "sky_badlands_01",
               "sky_hydro_01",
               "sky_night_01",
               "sky_nightfall_01",
               "sky_trainyard_01",
               "sky_stormfront_01",
               "sky_morningsnow_01",
               "sky_alpinestorm_01",
               "sky_harvest_01",
               "sky_harvest_night_01",
               "sky_halloween",
               "sky_halloween_night_01",
               "sky_halloween_night2014_01",
               "sky_island_01",
               "sky_jungle_01",
               "sky_invasion2fort_01",
               "sky_well_02",
               "sky_outpost_01",
               "sky_coastal_01",
               "sky_rainbow_01",
               "sky_badlands_pyroland_01",
               "sky_pyroland_01",
               "sky_pyroland_02",
               "sky_pyroland_03" });
static CatVar
    skybox_changer(skys, "skybox_changer", "0", "Change Skybox to this skybox",
                   "Change Skybox to this skybox, only changes on map load");
static CatVar halloween_mode(CV_SWITCH, "halloween_mode", "0",
                             "Forced Halloween mode",
                             "forced tf_forced_holiday 2");
void LevelInit_hook(void *_this, const char *newmap)
{
    static const LevelInit_t original =
        (LevelInit_t) hooks::clientmode.GetMethod(offsets::LevelInit());
    playerlist::Save();
    votelogger::antikick_ticks = 0;
    hacks::shared::lagexploit::bcalled = false;
    typedef bool *(*LoadNamedSkys_Fn)(const char *);
    uintptr_t addr =
        gSignatures.GetEngineSignature("55 89 E5 57 31 FF 56 8D B5 ? ? ? ? 53 "
                                       "81 EC ? ? ? ? C7 85 ? ? ? ? ? ? ? ?");
    static LoadNamedSkys_Fn LoadNamedSkys = LoadNamedSkys_Fn(addr);
    bool succ;
#ifdef __clang__
    asm("movl %1, %%edi; push skynum[(int) skybox_changer]; call %%edi; mov "
        "%%eax, %0; add %%esp, 4h"
        : "=r"(succ)
        : "r"(LoadNamedSkys));
#else
    succ = LoadNamedSkys(skynum[(int) skybox_changer]);
#endif
    logging::Info("Loaded Skybox: %s", succ ? "true" : "false");
    ConVar *holiday = g_ICvar->FindVar("tf_forced_holiday");

    if (halloween_mode)
        holiday->SetValue(2);
    else if (holiday->m_nValue == 2)
        holiday->SetValue(2);

    g_IEngine->ClientCmd_Unrestricted("exec cat_matchexec");
    hacks::shared::aimbot::Reset();
    chat_stack::Reset();
    hacks::shared::anticheat::ResetEverything();
    original(_this, newmap);
    hacks::shared::walkbot::OnLevelInit();
#if ENABLE_IPC
    if (ipc::peer)
    {
        ipc::peer->memory->peer_user_data[ipc::peer->client_id].ts_connected =
            time(nullptr);
    }
#endif
}

void LevelShutdown_hook(void *_this)
{
    static const LevelShutdown_t original =
        LevelShutdown_t(hooks::clientmode.GetMethod(offsets::LevelShutdown()));
    need_name_change = true;
    playerlist::Save();
    g_Settings.bInvalid = true;
    hacks::shared::aimbot::Reset();
    chat_stack::Reset();
    hacks::shared::anticheat::ResetEverything();
    original(_this);
#if ENABLE_IPC
    if (ipc::peer)
    {
        ipc::peer->memory->peer_user_data[ipc::peer->client_id]
            .ts_disconnected = time(nullptr);
    }
#endif
}
#if ENABLE_VISUALS == 1
int RandomInt_hook(void *_this, int iMinVal, int iMaxVal)
{
    static const RandomInt_t original =
        RandomInt_t(hooks::vstd.GetMethod(offsets::RandomInt()));

    if (medal_flip && iMinVal == 0 && iMaxVal == 9)
        return 0;

    return original(_this, iMinVal, iMaxVal);
}
#endif
