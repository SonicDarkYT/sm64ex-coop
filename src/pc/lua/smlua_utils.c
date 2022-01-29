#include "smlua.h"

u8 gSmLuaConvertSuccess = false;

#define VEC3F_BUFFER_COUNT 64
static Vec3f sVec3fBuffer[VEC3F_BUFFER_COUNT] = { 0 };
static u8 sVec3fBufferIndex = 0;

#define VEC3S_BUFFER_COUNT 64
static Vec3s sVec3sBuffer[VEC3S_BUFFER_COUNT] = { 0 };
static u8 sVec3sBufferIndex = 0;

f32* smlua_get_vec3f_from_buffer(void) {
    if (sVec3fBufferIndex > VEC3F_BUFFER_COUNT) { sVec3fBufferIndex = 0; }
    return sVec3fBuffer[sVec3fBufferIndex++];
}

s16* smlua_get_vec3s_from_buffer(void) {
    if (sVec3sBufferIndex > VEC3S_BUFFER_COUNT) { sVec3sBufferIndex = 0; }
    return sVec3sBuffer[sVec3sBufferIndex++];
}

void smlua_bind_function(lua_State* L, const char* name, void* func) {
    lua_pushcfunction(L, func);
    lua_setglobal(L, name);
}

void smlua_logline(void) {
    lua_State* L = gLuaState;
    lua_Debug info;
    int level = 0;
    while (lua_getstack(L, level, &info)) {
        lua_getinfo(L, "nSl", &info);
        LOG_INFO("  [%d] %s:%d -- %s [%s]",
            level, info.short_src, info.currentline,
            (info.name ? info.name : "<unknown>"), info.what);
        ++level;
    }
}

//////////////////////////////////////////////

lua_Integer smlua_to_integer(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TBOOLEAN) {
        return lua_toboolean(L, index) ? 1 : 0;
    } else if (lua_type(L, index) != LUA_TNUMBER) {
        LOG_LUA("LUA: smlua_to_integer received improper type '%d'", lua_type(L, index));
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return 0;
    }
    gSmLuaConvertSuccess = true;
    lua_Integer val = lua_tointeger(L, index);
    return (val == 0) ? lua_tonumber(L, index) : val;
}

lua_Number smlua_to_number(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TNUMBER) {
        LOG_LUA("LUA: smlua_to_number received improper type '%d'", lua_type(L, index));
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return 0;
    }
    gSmLuaConvertSuccess = true;
    return lua_tonumber(L, index);
}

const char* smlua_to_string(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TSTRING) {
        LOG_LUA("LUA: smlua_to_string received improper type '%d'", lua_type(L, index));
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return 0;
    }
    gSmLuaConvertSuccess = true;
    return lua_tostring(L, index);
}

void* smlua_to_cobject(lua_State* L, int index, u16 lot) {
    if (lua_type(L, index) != LUA_TTABLE) {
        LOG_LUA("LUA: smlua_to_cobject received improper type '%d'", lua_type(L, index));
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return 0;
    }

    // get LOT
    lua_getfield(L, index, "_lot");
    enum LuaObjectType objLot = smlua_to_integer(L, -1);
    lua_pop(L, 1);
    if (!gSmLuaConvertSuccess) { return NULL; }

    if (lot != objLot) {
        LOG_LUA("LUA: smlua_to_cobject received improper LOT. Expected '%d', received '%d'", lot, objLot);
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return NULL;
    }

    // get pointer
    lua_getfield(L, index, "_pointer");
    void* pointer = (void*)smlua_to_integer(L, -1);
    lua_pop(L, 1);
    if (!gSmLuaConvertSuccess) { return NULL; }

    // check allowlist
    if (!smlua_cobject_allowlist_contains(lot, (u64)pointer)) {
        LOG_LUA("LUA: smlua_to_cobject received a pointer not in allow list. '%u', '%llu", lot, (u64)pointer);
        gSmLuaConvertSuccess = false;
        return NULL;
    }

    if (pointer == NULL) {
        LOG_LUA("LUA: smlua_to_cobject received null pointer.");
        smlua_logline();
        gSmLuaConvertSuccess = false;
        return NULL;
    }

    gSmLuaConvertSuccess = true;
    return pointer;
}

//////////////////////////////////////////////

void smlua_push_object(lua_State* L, u16 lot, void* p) {
    if (p == NULL) {
        lua_pushnil(L);
        return;
    }
    // add to allowlist
    smlua_cobject_allowlist_add(lot, (u64)p);

    lua_newtable(L);
    int t = lua_gettop(L);
    smlua_push_integer_field(t, "_lot", lot);
    smlua_push_integer_field(t, "_pointer", (u64)p);
    lua_pushglobaltable(L);
    lua_getfield(gLuaState, -1, "_CObject");
    lua_setmetatable(L, -3);
    lua_pop(L, 1); // pop global table
}

void smlua_push_integer_field(int index, char* name, lua_Integer val) {
    lua_pushinteger(gLuaState, val);
    lua_setfield(gLuaState, index, name);
}

void smlua_push_number_field(int index, char* name, lua_Number val) {
    lua_pushnumber(gLuaState, val);
    lua_setfield(gLuaState, index, name);
}

lua_Integer smlua_get_integer_field(int index, char* name) {
    if (lua_type(gLuaState, index) != LUA_TTABLE) {
        LOG_LUA("LUA: smlua_get_integer_field received improper type '%d'", lua_type(gLuaState, index));
        gSmLuaConvertSuccess = false;
        return 0;
    }
    lua_getfield(gLuaState, index, name);
    lua_Integer val = smlua_to_integer(gLuaState, -1);
    lua_pop(gLuaState, 1);
    return val;
}

lua_Number smlua_get_number_field(int index, char* name) {
    if (lua_type(gLuaState, index) != LUA_TTABLE) {
        LOG_LUA("LUA: smlua_get_number_field received improper type '%d'", lua_type(gLuaState, index));
        gSmLuaConvertSuccess = false;
        return 0;
    }
    lua_getfield(gLuaState, index, name);
    lua_Number val = smlua_to_number(gLuaState, -1);
    lua_pop(gLuaState, 1);
    return val;
}

void smlua_dump_stack(void) {
    lua_State* L = gLuaState;
    int top = lua_gettop(L);
    printf("--------------\n");
    for (int i = 1; i <= top; i++) {
        printf("%d\t%s\t", i, luaL_typename(L, i));
        switch (lua_type(L, i)) {
        case LUA_TNUMBER:
            printf("%g\n", lua_tonumber(L, i));
            break;
        case LUA_TSTRING:
            printf("%s\n", lua_tostring(L, i));
            break;
        case LUA_TBOOLEAN:
            printf("%s\n", (lua_toboolean(L, i) ? "true" : "false"));
            break;
        case LUA_TNIL:
            printf("%s\n", "nil");
            break;
        default:
            printf("%p\n", lua_topointer(L, i));
            break;
        }
    }
    printf("--------------\n");
}

void smlua_dump_globals(void) {
    lua_State* L = gLuaState;
    printf("--------------\n");
    lua_pushglobaltable(L);

    // table is in the stack at index 't'
    lua_pushnil(L);  // first key
    while (lua_next(L, -2) != 0) {
        // uses 'key' (at index -2) and 'value' (at index -1)
        if (lua_type(L, -2) == LUA_TSTRING) {
            printf("%s - %s\n",
                lua_tostring(L, -2),
                lua_typename(L, lua_type(L, -1)));
        }
        else {
            printf("%s - %s\n",
                lua_typename(L, lua_type(L, -2)),
                lua_typename(L, lua_type(L, -1)));
        }
        // removes 'value'; keeps 'key' for next iteration
        lua_pop(L, 1);
    }
    lua_pop(L, 1);                 // remove global table(-1)
    printf("--------------\n");
}

void smlua_dump_table(int index) {
    lua_State* L = gLuaState;
    printf("--------------\n");

    // table is in the stack at index 't'
    lua_pushnil(L);  // first key
    while (lua_next(L, index) != 0) {
        // uses 'key' (at index -2) and 'value' (at index -1)
        if (lua_type(L, index) == LUA_TSTRING) {
            printf("%s - %s\n",
                lua_tostring(L, -2),
                lua_typename(L, lua_type(L, -1)));
        }
        else {
            printf("%s - %s\n",
                lua_typename(L, lua_type(L, -2)),
                lua_typename(L, lua_type(L, -1)));
        }
        // removes 'value'; keeps 'key' for next iteration
        lua_pop(L, 1);
    }
    printf("--------------\n");
}