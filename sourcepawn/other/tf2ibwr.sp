public Plugin:myinfo = 
{
	name        = "Bot Weapon Randomizer Skial version",
	author      = "Bottiger",
	description = "Gives bots random weapons and wearables",
	version     = "2.0",
	url         = "https://www.skial.com"
};

/*
EF_NODRAW doesn't hide weapons for some reason. neither does RENDERMODE_NONE.... only transparency render works wtf...
*/

// uncomment to dump weapon ids to chat and bypass current whitelist
//#define DUMP_WEAPONS
#define EF_NODRAW 32

#include <tf2>
#include <tf2_stocks>
#include <sdkhooks>
#include <regex>

#include <async2>
#include <tf2idb>
#include <tf2items>
#include <tf2attributes>

#undef REQUIRE_PLUGIN
#include <steamtest_plugin>

ConVar tf2items_bwr_disable_weapons;
ConVar tf2items_bwr_disable_wearables;
ConVar tf2items_bwr_dynamicmodels_limit;
ConVar tf2items_bwr_enable_unusuals;
ConVar tf2items_bwr_override_hat_level;
ConVar tf2items_bwr_blue_only;

ConVar steamtest_cloak;

Database g_db;
Json g_schema;
char g_idb_class_names[][] = {"invalid","scout","sniper","soldier","demoman","medic","heavy","pyro","spy","engineer"};
Handle hSDKEquipWearable;
Regex re_blacklisted_weapon_classes;

ArrayList g_particles;

StringMap g_equipment[SKIAL_MAX_PLAYERS+1];
StringMap g_used_indexes;
StringMap g_whitelist;

bool g_late;

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max) 
{
    g_late = late;
    return APLRes_Success;
}

public void OnPluginStart() 
{
    CreateWhitelist();

    char error[256];
    g_db = SQLite_UseDatabase("tf2idb", error, sizeof(error));
    if(g_db == INVALID_HANDLE)
    {
        SetFailState(error);
    }

    re_blacklisted_weapon_classes = new Regex("^(tf_weapon_lunchbox|tf_wearable|tf_weapon_buff_item|tf_weapon_laser_pointer|tf_weapon_parachute|tf_weapon_compound_bow|tf_weapon_rocketpack|saxxy)", 0, error, sizeof(error));
    if(re_blacklisted_weapon_classes == null)
    {
        SetFailState(error);
    }

    g_used_indexes = new StringMap();

    tf2items_bwr_disable_weapons = CreateConVar("tf2items_bwr_disable_weapons", "0", "Disable bot weapons for plugins that already give weapons like Randomizer");
    tf2items_bwr_disable_wearables = CreateConVar("tf2items_bwr_disable_wearables", "0", "Disable wearables");
    tf2items_bwr_enable_unusuals = CreateConVar("tf2items_bwr_enable_unusuals", "1", "Enable unusuals");
    tf2items_bwr_override_hat_level = CreateConVar("tf2items_bwr_override_hat_level", "1", "Set hat level to 69");
    tf2items_bwr_blue_only = CreateConVar("tf2items_bwr_blue_only", "0", "Only give items to team blue");

    tf2items_bwr_dynamicmodels_limit = CreateConVar("tf2items_bwr_dynamicmodels_limit", "0", "Only use items seen already if dynamicmodels table is over this number. Can set to 0 to disable if dynamicmodels fix is on.");

    HookEvent("post_inventory_application", OnInventory);

    g_schema = Json.CreateObject();

    #if defined DUMP_WEAPONS
    char dump_clause[] = ",i.name";
    #else
    char dump_clause[] = "";
    #endif

    char slot_names[][] = {"primary", "secondary", "melee"};
    for(int i=1;i<=view_as<int>(TFClass_Engineer);i++)
    {
        char query[256];
        for(int j=0;j<3;j++)
        {
            g_db.Format(query, sizeof(query),
                "SELECT c.id,i.class%s FROM tf2idb_class c JOIN tf2idb_item i ON i.id=c.id "...
                "WHERE c.class=\"%s\" AND c.slot=\"%s\" AND i.has_string_attribute=0 AND c.id > 30 AND NOT (c.id >= 15000 AND c.id <= 15158) AND i.name NOT LIKE '%%botkiller%%'",
                dump_clause,g_idb_class_names[i], slot_names[j]);
            DataPack pack = new DataPack();
            pack.WriteCell(i);
            pack.WriteCell(j);
            pack.Reset();
            g_db.Query(OnWeaponQueryComplete, query, pack);
        }

        g_db.Format(query, sizeof(query), 
            "SELECT b.id "...
            "FROM tf2idb_class b "...
            "JOIN tf2idb_equip_regions c ON b.id=c.id AND c.region != 'medal'"...
            "WHERE b.class=\"%s\" AND b.slot='misc'"...
            "GROUP BY b.id", g_idb_class_names[i]);
        g_db.Query(OnMiscQueryComplete, query, i);

        g_db.Format(query, sizeof(query), "SELECT b.id FROM tf2idb_class b WHERE b.class=\"%s\" AND b.slot='head'", g_idb_class_names[i]);
        g_db.Query(OnHatQueryComplete, query, i);
    }

    g_db.Query(OnParticleQueryComplete, "SELECT id FROM tf2idb_particles WHERE type='cosmetic_unusual_effects' AND id >= 80");

    SetupNativeCalls();

    if(g_late)
    {
        for(int i=1;i<=MaxClients;i++)
        {
            if(IsClientInGame(i))
            {
                OnClientPutInServer(i);
            }
        }
    }
}

void SetupNativeCalls()
{
    decl String:strFilePath[PLATFORM_MAX_PATH];
    BuildPath( Path_SM, strFilePath, sizeof(strFilePath), "gamedata/skial.txt" ); //bottiger edit
    if( FileExists( strFilePath ) )
    {
        new Handle:hGameConf = LoadGameConfigFile( "skial" ); //bottiger edit
        if( hGameConf != INVALID_HANDLE )
        {
            StartPrepSDKCall(SDKCall_Player);
            PrepSDKCall_SetFromConf( hGameConf, SDKConf_Signature, "CTFPlayer::EquipWearable" ); //bottiger edit
            PrepSDKCall_AddParameter( SDKType_CBaseEntity, SDKPass_Pointer );
            hSDKEquipWearable = EndPrepSDKCall();
            if( hSDKEquipWearable == INVALID_HANDLE )
                LogError("Could not initialize call for CTFPlayer::EquipWearable. Disabling wearables." );
            CloseHandle(hGameConf);
        }
    }
    else
    {
        LogError("Could not locate CTFPlayer::EquipWearable from skial.txt. Disabling wearables." );    
    }
}

public void OnEntityCreated(int ent, const char[] classname)
{
    if(StrEqual(classname, "tf_wearable"))
    {
        SDKHook(ent, SDKHook_SpawnPost, OnWearableSpawned);
    }
}

void OnWearableSpawned(int ent)
{
    int idx = GetEntProp(ent, Prop_Send, "m_iItemDefinitionIndex");
    char idxString[16];
    IntToString(idx, idxString, sizeof(idxString));
    g_used_indexes.SetValue(idxString, 1);
}

public OnMapEnd()
{
    g_used_indexes.Clear();
}

public OnClientPutInServer(int client)
{
    if(IsFakeClient(client))
    {
        SDKHook(client, SDKHook_WeaponSwitchPost, OnWeaponSwitchPost)
    }
}

public OnClientDisconnect(int client)
{
    if(!IsFakeClient(client))
    {
        return;
    }

    if(g_equipment[client])
    {
        delete g_equipment[client];
        g_equipment[client] = null;
    }
}

// fix bot weapons becoming visible even when not equipped
public void OnWeaponSwitchPost(int client, int weapon)
{
    if(weapon == -1)
    {
        return;
    }

    SetEntityRenderMode(weapon, RENDER_NORMAL);
    for(int i=0;i<3;i++)
    {
        int otherWeapon = GetPlayerWeaponSlot(client, i);
        if(otherWeapon == weapon || !IsValidEntity(otherWeapon))
        {
            continue;
        }
        SetEntityRenderMode(otherWeapon, RENDER_TRANSALPHA);
        SetEntityRenderColor(otherWeapon, 255, 255, 255, 0);
    }
}

public Action OnInventory(Handle event, char[] name, bool dontBroadcast)
{   
    int userid = GetEventInt(event, "userid");
    int client = GetClientOfUserId(userid);

    if(!IsFakeClient(client))
    {
        return Plugin_Continue;
    }

    if(tf2items_bwr_blue_only.BoolValue && GetClientTeam(client) != 3)
    {
        return Plugin_Continue;
    }

    CreateTimer(0.01, OnInventoryPost, userid, TIMER_FLAG_NO_MAPCHANGE);

    return Plugin_Continue;
}

public Action OnInventoryPost(Handle timer, int userid)
{
    int client = GetClientOfUserId(userid);
    if(client == 0 || !IsFakeClient(client))
    {
        return Plugin_Handled;
    }

    // in case bot gets a team switch after inventory
    if(tf2items_bwr_blue_only.BoolValue && GetClientTeam(client) != 3)
    {
        return Plugin_Handled;
    }

    EquipBot(client);
    return Plugin_Handled;
}

void GenerateWeapons(int client, TFClassType class)
{
    // check if we have items for this class already
    int junk;
    char key_class[32];
    Format(key_class, sizeof(key_class), "class_%i", class);
    if(g_equipment[client].GetValue(key_class, junk))
    {
        return;
    }
    g_equipment[client].SetValue(key_class, 1);

    for(int i=0;i<3;i++)
    {
        char key[32];
        Format(key, sizeof(key), "%i_%i", class, i);

        // get weapons from sql
        if(g_schema.HasKey(key))
        {
            Json list = g_schema.GetArray(key);
            int length = list.ArrayLength;
            if(length > 0)
            {
                int rand = GetRandomInt(0, length-1);
                int id = list.ArrayGetInt(rand);

                char key_id[32];
                Format(key_id, sizeof(key_id), "weapon_%i_%i", class, i);
                g_equipment[client].SetValue(key_id, id);

                char key_lvl[32];
                Format(key_lvl, sizeof(key_lvl), "weapon_level_%i_%i", class, i);
                g_equipment[client].SetValue(key_lvl, GetRandomInt(1, 99));
            }
            list.Close();
        }
    }
}

void GenerateWearables(int client, TFClassType class)
{
    // check if we have items for this class already
    int junk;
    char key_class[32];
    Format(key_class, sizeof(key_class), "class_wear_%i", class);
    if(g_equipment[client].GetValue(key_class, junk))
    {
        return;
    }
    g_equipment[client].SetValue(key_class, 1);

    // make new wearables and save
    bool useOldItems = tf2items_bwr_dynamicmodels_limit.IntValue > 0 && GetDynamicModelsCount() > tf2items_bwr_dynamicmodels_limit.IntValue;

    char key[32];
    Format(key, sizeof(key), "%i_hats", class);
    if(g_schema.HasKey(key))
    {
        Json list = g_schema.GetArray(key);
        if(useOldItems)
        {
            list = ReplaceListWithUsedItems(list);
        }
        int length = list.ArrayLength;
        if(length > 0)
        {
            int rand = GetRandomInt(0, length-1);
            int id = list.ArrayGetInt(rand);

            char key2[32];
            Format(key2, sizeof(key2), "hat_%i", class);
            g_equipment[client].SetValue(key2, id);
            Format(key2, sizeof(key2), "hat_level_%i", class);
            g_equipment[client].SetValue(key2, GetRandomInt(1, 99));
            Format(key2, sizeof(key2), "hat_color_%i", class);
            g_equipment[client].SetValue(key2, GetRandomInt(1, 11));
            Format(key2, sizeof(key2), "hat_unusual_%i", class);
            g_equipment[client].SetValue(key2, GetRandomUnusual());
        }
        list.Close();
    }

    Format(key, sizeof(key), "%i_miscs", class);
    if(g_schema.HasKey(key))
    {
        Json list = g_schema.GetArray(key);
        if(useOldItems)
        {
            list = ReplaceListWithUsedItems(list);
        }
        int length = list.ArrayLength;
        if(length > 0)
        {
            int rand = GetRandomInt(0, length-1);
            int id = list.ArrayGetInt(rand);

            char key2[32];
            Format(key2, sizeof(key2), "misc1_%i", class);
            g_equipment[client].SetValue(key2, id);
            Format(key2, sizeof(key2), "misc1_level_%i", class);
            g_equipment[client].SetValue(key2, GetRandomInt(1, 99));
            Format(key2, sizeof(key2), "misc1_color_%i", class);
            g_equipment[client].SetValue(key2, GetRandomInt(1, 11));
        }
        list.Close();
    }
}

void EquipBot(int client)
{
    TFClassType class = TF2_GetPlayerClass(client);

    if(g_equipment[client] == null)
    {
        g_equipment[client] = new StringMap();
    }

    GenerateWeapons(client, class);
    GenerateWearables(client, class);

    if(tf2items_bwr_disable_weapons.BoolValue == false)
    {
        for(int i=2;i>=0;i--)
        {
            int weapon_id;
            int weapon_level;

            char key[32];
            Format(key, sizeof(key), "weapon_%i_%i", class, i);
            g_equipment[client].GetValue(key, weapon_id);
            Format(key, sizeof(key), "weapon_level_%i_%i", class, i);
            g_equipment[client].GetValue(key, weapon_level);

            if(weapon_id == 0)
            {
                continue;
            }

            int weapon = EquipFakeWeapon(client, weapon_id, weapon_level);
            //HideWeapon(weapon);
            //AcceptEntityInput(weapon, "HideWeapon");
            SetEntityRenderMode(weapon, RENDER_TRANSALPHA);
            SetEntityRenderColor(weapon, 255, 255, 255, 0);
        }

        // some clients don't have a weapon at a specific slot, this must be done to stop civilian
        // need to force unhiding if we switch to a different slot
        for(int i=0;i<3;i++)
        {
            int weapon = GetPlayerWeaponSlot(client, i);
            if(IsValidEntity(weapon))
            {
                SetEntPropEnt(client, Prop_Send, "m_hActiveWeapon", weapon);
                //ShowWeapon(weapon);
                SetEntityRenderMode(weapon, RENDER_NORMAL);
                break;
            }
        }
    }

    if(tf2items_bwr_disable_wearables.BoolValue == false)
    {
        int id;
        int level;
        int color;
        int unusual;

        char key[32];
        Format(key, sizeof(key), "hat_%i", class);
        g_equipment[client].GetValue(key, id);
        if(id != 0)
        {
            Format(key, sizeof(key), "hat_level_%i", class);
            g_equipment[client].GetValue(key, level);
            Format(key, sizeof(key), "hat_color_%i", class);
            g_equipment[client].GetValue(key, color);
            Format(key, sizeof(key), "hat_unusual_%i", class);
            g_equipment[client].GetValue(key, unusual);

            if(tf2items_bwr_override_hat_level.BoolValue)
            {
                level = 69;
            }
            
            int quality = unusual ? 5 : 1;
            int item = EquipFakeWearable(client, id, level, quality);
            ApplyPaint(item, color);
            if(tf2items_bwr_enable_unusuals.BoolValue)
                ApplyUnusual(item, unusual);
        }

        int misc1_id;
        int misc1_level;
        int misc1_color;

        Format(key, sizeof(key), "misc1_%i", class);
        g_equipment[client].GetValue(key, misc1_id);
        if(misc1_id != 0)
        {
            Format(key, sizeof(key), "misc1_level_%i", class);
            g_equipment[client].GetValue(key, misc1_level);
            Format(key, sizeof(key), "misc1_color_%i", class);
            g_equipment[client].GetValue(key, misc1_color);

            int item = EquipFakeWearable(client, misc1_id, misc1_level, 1);
            ApplyPaint(item, misc1_color);
        }
    }
}

void ApplyPaint(int item, int random=0)
{
    if(random == 0)
    {
        random = GetRandomInt(1, 11);
    }

    switch(random)
    {
        case 1:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(1315860)); // black
        }
        case 2:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(15132390)); // white
        }
        case 3:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(15185211)); // gold
        }
        case 4:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(8208497)); // purple
        }
        case 5:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(7511618)); // green
        }
        case 6:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(16738740)); // pink
        }
        case 7:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(3329330)); // lime
        }
        case 8:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(0xC36C2D)); // cream spirit
            TF2Attrib_SetByDefIndex(item, 261, float(0xB88035));
        }
        case 9:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(0x3B1F23)); // baclava's are forever
            TF2Attrib_SetByDefIndex(item, 261, float(0x18233D));
        }
        case 10:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(0x803020)); // value of teamwork
            TF2Attrib_SetByDefIndex(item, 261, float(0x256D8D));
        }
        case 11:
        {
            TF2Attrib_SetByDefIndex(item, 142, float(0xA89A8C)); // water logged labcoat
            TF2Attrib_SetByDefIndex(item, 261, float(0x839FA3));
        }
        default:
        {

        }
    }
}

void ApplyUnusual(int item, int id)
{
    if(id == 0)
    {
        return;
    }

    TF2Attrib_SetByDefIndex(item, 134, float(id));
}

int GetRandomUnusual()
{
    if(g_particles && GetRandomInt(1, 10) == 1)
    {
        int random = GetRandomInt(0, g_particles.Length-1);
        int particle = g_particles.Get(random);
        return particle;
    }
    return 0;
}

// filter list with only those used before to avoid filling up the dynamicmodels list
// only wearables seems to fill this up
// the old list is closed, the list returned must be closed by caller
Json ReplaceListWithUsedItems(Json old)
{
    Json newList = Json.CreateArray();
    int length = old.ArrayLength;
    for(int i=0;i<length;i++)
    {
        int id = old.ArrayGetInt(i);
        char idString[16];
        IntToString(id, idString, sizeof(idString));
        int junk;
        if(!g_used_indexes.GetValue(idString, junk))
        {
            continue;
        }
        newList.ArrayAppendInt(id);
    }
    old.Close();
    return newList;
}

public Action TF2Items_OnGiveNamedItem(int client, char[] originalClassName, int originalId, Handle &item)
{
    if(!IsFakeClient(client))
    {
        return Plugin_Continue;
    }
    return Plugin_Continue;
}

public void OnWeaponQueryComplete(Database db, DBResultSet results, const char[] error, DataPack pack)
{
    int class = pack.ReadCell();
    int slot = pack.ReadCell();

    delete pack;

    if(results == null)
    {
        SetFailState("OnWeaponQueryComplete error: %s", error);
    }

    #if defined DUMP_WEAPONS
    PrintToServer("// class %i slot %i", class, slot);
    #endif

    Json list = Json.CreateArray();
    while(results.FetchRow())
    {
        int id = results.FetchInt(0);
        char itemClass[32];
        results.FetchString(1, itemClass, sizeof(itemClass));

        if(itemClass[0] == 0 || re_blacklisted_weapon_classes.Match(itemClass) > 0)
        {
            continue;
        }

        if(IsWeaponBlacklisted(id))
        {
            continue;
        }

        #if !defined DUMP_WEAPONS
        if(IsWeaponWhitelisted(id) == false)
        {
            continue;
        }
        #endif

        list.ArrayAppendInt(id);

        #if defined DUMP_WEAPONS
        char itemName[64];
        results.FetchString(2, itemName, sizeof(itemName));
        PrintToServer("%i, // %s", id, itemName);
        #endif
    }
    char key[32];
    Format(key, sizeof(key), "%i_%i", class, slot);
    g_schema.SetObject(key, list);
    list.Close();
}

public void OnMiscQueryComplete(Database db, DBResultSet results, const char[] error, int class)
{
    if(results == null)
    {
        SetFailState("OnMiscQueryComplete error: %s", error);
    }

    Json list = Json.CreateArray();
    while(results.FetchRow())
    {
        int id = results.FetchInt(0);
        list.ArrayAppendInt(id);
    }
    char key[32];
    Format(key, sizeof(key), "%i_miscs", class);
    g_schema.SetObject(key, list);
    list.Close();
}

public void OnHatQueryComplete(Database db, DBResultSet results, const char[] error, int class)
{
    if(results == null)
    {
        SetFailState("OnHatQueryComplete error: %s", error);
    }

    Json list = Json.CreateArray();
    while(results.FetchRow())
    {
        int id = results.FetchInt(0);
        if(IsHatBlacklisted(id))
        {
            continue;
        }
        list.ArrayAppendInt(id);
    }
    char key[32];
    Format(key, sizeof(key), "%i_hats", class);
    g_schema.SetObject(key, list);
    list.Close();
}

public void OnParticleQueryComplete(Database db, DBResultSet results, const char[] error, int class)
{
    if(results == null)
    {
        SetFailState("OnParticleQueryComplete error: %s", error);
    }

    g_particles = new ArrayList();
    while(results.FetchRow())
    {
        int id = results.FetchInt(0);
        g_particles.Push(id);
    }
}

int EquipFakeWeapon(int client, int index, int level)
{
    if(index < 0)
    {
        ThrowError("Invalid index passed to EquipFakeWeapon: %i", index);
        return -1;
    }
    
    char weaponClass[64];
    TF2IDB_GetItemClass(index, weaponClass, sizeof(weaponClass));

    // do not equip wearable weapons will CRASH!
    if(IsWearableWeapon(weaponClass))
    {
        return -1;
    }

    TFClassType class = TF2_GetPlayerClass(client);

    if(StrEqual(weaponClass, "tf_weapon_shotgun"))
    {
        switch(class)
        {
            case TFClass_Heavy:
            {
                weaponClass = "tf_weapon_shotgun_hwg";
            }
            case TFClass_Soldier:
            {
                weaponClass = "tf_weapon_shotgun_soldier";
            }
            case TFClass_Pyro:
            {
                weaponClass = "tf_weapon_shotgun_pyro";
            }
            default:
            {
                weaponClass = "tf_weapon_shotgun_primary";
            }
        }
    }

    int flags = OVERRIDE_ALL;
    if(!StrEqual(weaponClass, "saxxy"))
    {
        flags = flags|PRESERVE_ATTRIBUTES|FORCE_GENERATION;
    }
    else
    {
        level = 69; // make saxxy's level 69
    }

    Handle tf2item = TF2Items_CreateItem(flags);
    TF2Items_SetClassname(tf2item, weaponClass);
    TF2Items_SetItemIndex(tf2item, index);
    TF2Items_SetLevel(tf2item, level);
    TF2Items_SetQuality(tf2item, 0);

    int slot = GetItemSlot(index, class);
    int oldWeapon = GetPlayerWeaponSlot(client, slot);
    //PrintToServer("old weapon %i owner %i client %i", oldWeapon, GetEntProp(oldWeapon, Prop_Send, "m_iAccountID"), client);
    if(IsValidEntity(oldWeapon) && GetEntProp(oldWeapon, Prop_Send, "m_iItemDefinitionIndex") == index)
    {
        return oldWeapon;
    }
    TF2_RemoveWeaponSlot(client, slot);                 // remove normal weapons
    int item = TF2Items_GiveNamedItem(client, tf2item);
    CloseHandle(tf2item);

    if(!IsValidEntity(item))
    {
        LogError("TF2Items_GiveNamedItem failed in EquipFakeWeapon");
        return -1;
    }

    SetEntProp(item, Prop_Send, "m_bValidatedAttachedEntity", 1);
    EquipPlayerWeapon(client, item);

    // needed to prevent bots from dropping weapons or saying they are holding a weapon from a random player
    if(steamtest_cloak == null)
    {
        steamtest_cloak = FindConVar("steamtest_cloak");
    }

    if(steamtest_cloak && steamtest_cloak.BoolValue)
    {
        int acc = SteamTest_GetBotFriendsID(client);
        SetEntProp(item, Prop_Send, "m_iAccountID", acc);
    }

    return item;
}

int EquipFakeWearable(int client, int index, int level, int quality)
{
    if(index <= 0)
    {
        LogError("Invalid index passed to EquipFakeWearable: %i", index);
        ThrowError("Invalid index passed to EquipFakeWearable: %i", index);
        return -1;
    }

    int item = CreateEntityByName("tf_wearable");
    if(!IsValidEntity(item))
    {
        LogError("failed to make tf_wearable");
        return -1;
    }

    SetEntProp(item, Prop_Send, "m_iItemDefinitionIndex", index);
    SetEntProp(item, Prop_Send, "m_bInitialized", 1);
    SetEntProp(item, Prop_Send, "m_iEntityLevel", level);
    SetEntProp(item, Prop_Send, "m_iEntityQuality", quality);
    DispatchSpawn(item);
    SetEntProp(item, Prop_Send, "m_bValidatedAttachedEntity", 1);

    /*
    Handle tf2item = TF2Items_CreateItem(OVERRIDE_ALL|PRESERVE_ATTRIBUTES|FORCE_GENERATION);
    TF2Items_SetClassname(tf2item, "tf_wearable");
    TF2Items_SetItemIndex(tf2item, index);
    TF2Items_SetLevel(tf2item, level);
    TF2Items_SetQuality(tf2item, quality);
    int item = TF2Items_GiveNamedItem(client, tf2item);
    CloseHandle(tf2item);
    if(!IsValidEntity(item))
    {
        LogError("failed to make tf_wearable");
        return -1;
    }
    */

    if(steamtest_cloak == null)
    {
        steamtest_cloak = FindConVar("steamtest_cloak");
    }

    if(steamtest_cloak && steamtest_cloak.BoolValue)
    {
        int acc = SteamTest_GetBotFriendsID(client);
        SetEntProp(item, Prop_Send, "m_iAccountID", acc);
    }    

    SDKCall(hSDKEquipWearable, client, item);
    return item;
}

// wrapper to fix tf2idb to get slots that can be used with removewearable
int GetItemSlot(int id, TFClassType class=TFClass_Unknown)
{
    TF2ItemSlot slot = TF2IDB_GetItemSlot(id, class);
    switch(slot)
    {
        case TF2ItemSlot_Secondary:
        {
            if(class == TFClass_Spy)
            {
                return TFWeaponSlot_Primary;
            }
        }
        case TF2ItemSlot_Building:
        {
            return TFWeaponSlot_Secondary;
        }
        case TF2ItemSlot_PDA1,TF2ItemSlot_PDA2:
        {
            return TFWeaponSlot_Building;
        }
    }

    return view_as<int>(slot);
}

// wearable weapons can only be spawned in TF2Items_OnGiveNamedItem or the server will crash
bool IsWearableWeapon(char[] class)
{
    if(strncmp(class, "tf_wearable", 11) == 0)
    {
        return true;
    }
    return false;
}

stock int GetDynamicModelsCount()
{
    int dynamicmodels = FindStringTable("DynamicModels");
    if(dynamicmodels == INVALID_STRING_TABLE)
    {
        return -1;
    }
    int strings = GetStringTableNumStrings(dynamicmodels);
    return strings;
}

bool IsHatBlacklisted(int id)
{
    switch(id)
    {
        case 640: // top notch
        {
            return true;
        }
        case 711,712,713: // dueler, gifting man, philateler
        {
            return true;
        }
        case 134,136,138: // custom pillar of hats
        {
            return true;
        }
        case 260,261: // mann co and wiki hat
        {
            return true;
        }
    }
    return false;
}

bool IsWeaponBlacklisted(int id)
{
    switch(id)
    {
        case 998: // vaccinator
        {
            return true;
        }
        case 237,265:// jumpers
        {
            return true;
        }
        case 730: // beggar's bazooka
        {
            return true;
        }
        case 594: // phlog
        {
            return true;
        }
        case 850: // deflector
        {
            return true;
        }
        case 1184: // gloves of running urgently MvM
        {
            return true;
        }
    }

    return false;
}

// this whitelist is created by enabling the DUMP_WEAPONS define at the top of this plugin
void CreateWhitelist()
{
    int whitelist[] = {
    // class 1 slot 0
    45, // Force-A-Nature
    200, // Scattergun
    220, // Shortstop
    448, // Soda Popper
    669, // Festive Scattergun
    772, // Baby Face's Blaster
    1078, // Festive Force-A-Nature
    1103, // Back Scatter
    // class 1 slot 1
    160, // Lugermorph
    209, // Pistol
    222, // Mad Milk
    294, // Lugermorph
    449, // Winger
    773, // Pretty Boy's Pocket Pistol
    812, // Flying Guillotine
    1121, // Mutated Milk
    30666, // C.A.P.P.E.R
    // class 1 slot 2
    44, // Sandman
    190, // Bat
    221, // Holy Mackerel
    317, // Candy Cane
    325, // Boston Basher
    349, // Sun-on-a-Stick
    355, // Fan O'War
    450, // Atomizer
    452, // Three-Rune Blade
    572, // Unarmed Combat
    648, // Wrap Assassin
    660, // Festive Bat
    999, // Festive Holy Mackerel
    30667, // Batsaber

    // class 2 slot 0
    201, // Sniper Rifle
    230, // Sydney Sleeper
    402, // Bazaar Bargain
    526, // Machina
    664, // Festive Sniper Rifle
    752, // Hitman's Heatmaker
    851, // AWPer Hand
    1098, // Classic
    30665, // Shooting Star
    // class 2 slot 1
    58, // Jarate
    203, // SMG
    751, // Cleaner's Carbine
    1083, // Festive Jarate
    1105, // Self-Aware Beauty Mark
    1149, // Festive SMG
    // class 2 slot 2
    171, // Tribalman's Shiv
    193, // Kukri
    232, // Bushwacka
    401, // Shahanshah

    // class 3 slot 0
    127, // Direct Hit
    205, // Rocket Launcher
    228, // Black Box
    414, // Liberty Launcher
    441, // Cow Mangler 5000
    513, // Original
    658, // Festive Rocket Launcher
    1085, // Festive Black Box
    1104, // Air Strike
    // class 3 slot 1
    199, // Shotgun
    415, // Reserve Shooter
    442, // Righteous Bison
    1141, // Festive Shotgun
    1153, // Panic Attack
    // class 3 slot 2
    128, // Equalizer
    154, // Pain Train
    196, // Shovel
    357, // Half-Zatoichi
    416, // Market Gardener
    447, // Disciplinary Action
    775, // Escape Plan

    // class 4 slot 0
    206, // Grenade Launcher
    308, // Loch-n-Load
    996, // Loose Cannon
    1007, // Festive Grenade Launcher
    // class 4 slot 1
    130, // Scottish Resistance
    207, // Stickybomb Launcher
    661, // Festive Stickybomb Launcher
    // class 4 slot 2
    132, // Eyelander
    154, // Pain Train
    172, // Scotsman's Skullcutter
    191, // Bottle
    266, // Horseless Headless Horsemann's Headtaker
    307, // Ullapool Caber
    327, // Claidheamh Mòr
    357, // Half-Zatoichi
    404, // Persian Persuader
    482, // Nessie's Nine Iron
    609, // Scottish Handshake
    1082, // Festive Eyelander

    // class 5 slot 0
    36, // Blutsauger
    204, // Syringe Gun
    305, // Crusader's Crossbow
    412, // Overdose
    1079, // Festive Crusader's Crossbow
    // class 5 slot 1
    35, // Kritzkrieg
    211, // Medi Gun
    411, // Quick-Fix
    663, // Festive Medi Gun
    // class 5 slot 2
    37, // Ubersaw
    173, // Vita-Saw
    198, // Bonesaw
    304, // Amputator
    413, // Solemn Vow
    1003, // Festive Ubersaw
    1143, // Festive Bonesaw

    // class 6 slot 0
    41, // Natascha
    202, // Minigun
    298, // Iron Curtain
    312, // Brass Beast
    424, // Tomislav
    654, // Festive Minigun
    811, // Huo-Long Heater
    // class 6 slot 1
    199, // Shotgun
    425, // Family Business
    1141, // Festive Shotgun
    1153, // Panic Attack
    // class 6 slot 2
    43, // Killing Gloves of Boxing
    195, // Fists
    239, // Gloves of Running Urgently
    310, // Warrior's Spirit
    331, // Fists of Steel
    426, // Eviction Notice
    587, // Apoco-Fists
    656, // Holiday Punch
    1084, // Festive Gloves of Running Urgently
    1100, // Bread Bite

    // class 7 slot 0
    40, // Backburner
    208, // Flame Thrower
    215, // Degreaser
    659, // Festive Flame Thrower
    741, // Rainblower
    1146, // Festive Backburner
    1178, // Dragon's Fury
    30474, // Nostromo Napalmer
    // class 7 slot 1
    39, // Flare Gun
    199, // Shotgun
    351, // Detonator
    415, // Reserve Shooter
    595, // Manmelter
    740, // Scorch Shot
    1081, // Festive Flare Gun
    1141, // Festive Shotgun
    1153, // Panic Attack
    1180, // Gas Passer
    // class 7 slot 2
    38, // Axtinguisher
    153, // Homewrecker
    192, // Fire Axe
    214, // Powerjack
    326, // Back Scratcher
    348, // Sharpened Volcano Fragment
    457, // Postal Pummeler
    466, // Maul
    593, // Third Degree
    739, // Lollichop
    813, // Neon Annihilator
    834, // Neon Annihilator
    1000, // Festive Axtinguisher
    1181, // Hot Hand

    // class 8 slot 0
    // class 8 slot 1
    61, // Ambassador
    161, // Big Kill
    210, // Revolver
    224, // L'Etranger
    460, // Enforcer
    525, // Diamondback
    1006, // Festive Ambassador
    1142, // Festive Revolver
    // class 8 slot 2
    194, // Knife
    225, // Your Eternal Reward
    356, // Conniver's Kunai
    461, // Big Earner
    574, // Wanga Prick
    638, // Sharp Dresser
    649, // Spy-cicle
    665, // Festive Knife
    727, // Black Rose

    // class 9 slot 0
    141, // Frontier Justice
    199, // Shotgun
    527, // Widowmaker
    588, // Pomson 6000
    997, // Rescue Ranger
    1004, // Festive Frontier Justice
    1141, // Festive Shotgun
    1153, // Panic Attack
    // class 9 slot 1
    160, // Lugermorph
    209, // Pistol
    294, // Lugermorph
    528, // Short Circuit
    30666, // C.A.P.P.E.R
    // class 9 slot 2
    142, // Gunslinger
    155, // Southern Hospitality
    169, // Golden Wrench
    197, // Wrench
    329, // Jag
    589, // Eureka Effect
    662, // Festive Wrench
    };

    g_whitelist = new StringMap();

    for(int i=0;i<sizeof(whitelist);i++)
    {
        int id = whitelist[i];
        char idString[32];
        IntToString(id, idString, sizeof(idString));
        g_whitelist.SetValue(idString, 1);
    }
}

bool IsWeaponWhitelisted(int id)
{
    char idString[32];
    IntToString(id, idString, sizeof(idString));
    int ignore;
    return g_whitelist.GetValue(idString, ignore);
}

/*
void HideWeapon(int weapon)
{
    int effects = GetEntProp(weapon, Prop_Send, "m_fEffects");
    SetEntProp(weapon, Prop_Send, "m_fEffects", effects | EF_NODRAW);
}

void ShowWeapon(int weapon)
{
    int effects = GetEntProp(weapon, Prop_Send, "m_fEffects");
    SetEntProp(weapon, Prop_Send, "m_fEffects", effects & ~EF_NODRAW);
}
*/