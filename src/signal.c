#include "queue.h"
#include "signames.h"
#include <lua.h>
#include <lauxlib.h>
#include <signal.h>
#include <string.h>

#define REG_TABLE "luasignal"

static lua_State* gL = NULL;
static lua_Hook old_hook = NULL;
static int old_mask = 0;
static int old_count = 0;
static queue q;
/* hardcoding 256 here is not great... is there a better way to get the highest
 * numbered signal? */
static struct sigaction lua_handlers[256];

static void lua_signal_handler(lua_State* L, lua_Debug* D)
{
    sigset_t sset, oldset;
    int sig;

    lua_sethook(gL, old_hook, old_mask, old_count);

    sigfillset(&sset);
    sigprocmask(SIG_BLOCK, &sset, &oldset);

    while ((sig = dequeue(&q)) != -1) {
        const char* signame;

        signame = sig_to_name(sig);
        lua_getfield(gL, LUA_REGISTRYINDEX, REG_TABLE);
        lua_getfield(gL, -1, signame);
        lua_pushstring(gL, signame);
        lua_call(gL, 1, 0);
    }

    sigprocmask(SIG_SETMASK, &oldset, NULL);
}

static void signal_handler(int sig)
{
    if (q.size == 0) {
        old_hook  = lua_gethook(gL);
        old_mask  = lua_gethookmask(gL);
        old_count = lua_gethookcount(gL);
        lua_sethook(gL, lua_signal_handler,
                    LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
    }

    enqueue(&q, sig);
}

static int l_signal(lua_State* L)
{
    const char* signame;
    int sig;
    struct sigaction sa;
    sigset_t sset;
    void (*handler)(int) = NULL;

    gL = L;

    signame = luaL_checklstring(gL, 1, NULL);
    sig = name_to_sig(signame);
    if (sig == -1) {
        lua_pushfstring(gL, "signal() called with invalid signal name: %s", signame);
        lua_error(gL);
    }

    if (lua_isfunction(gL, 2)) {
        handler = signal_handler;
        lua_getfield(gL, LUA_REGISTRYINDEX, REG_TABLE);
        lua_pushvalue(gL, 2);
        lua_setfield(gL, -2, signame);
    }
    else if (lua_isstring(gL, 2)) {
        const char* pseudo_handler;

        pseudo_handler = lua_tostring(gL, 2);
        if (strcmp(pseudo_handler, "ignore") == 0) {
            handler = SIG_IGN;
        }
        else if (strcmp(pseudo_handler, "cdefault") == 0) {
            handler = SIG_DFL;
        }
        else if (strcmp(pseudo_handler, "default") == 0) {
            if (lua_handlers[sig].sa_handler != NULL) {
                handler = lua_handlers[sig].sa_handler;
            }
            else {
                return 0;
            }
        }
        else {
            lua_pushstring(gL, "Must pass a valid handler to signal()");
            lua_error(gL);
        }
    }
    else {
        lua_pushstring(gL, "Must pass a handler to signal()");
        lua_error(gL);
    }
    sa.sa_handler = handler;
    sigfillset(&sset);
    sa.sa_mask = sset;
    if (lua_handlers[sig].sa_handler == NULL) {
        sigaction(sig, &sa, &(lua_handlers[sig]));
    }
    else {
        sigaction(sig, &sa, NULL);
    }

    return 0;
}

const luaL_Reg reg[] = {
    { "signal", l_signal },
    { NULL,     NULL },
};

int luaopen_signal(lua_State* L)
{
    queue_init(&q, 4);

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, REG_TABLE);

    luaL_register(L, "signal", reg);

    return 1;
}
