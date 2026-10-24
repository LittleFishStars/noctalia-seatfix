/*
 * noctalia-seat-filter.c
 *
 * 背景：niri (shorin fork) 把所有 wl_seat 全局都暴露给普通客户端，
 * 其中包括自动化用的 "niri-agent" seat。Noctalia 5 的 WaylandConnection
 * 在遇到 wl_seat global 时直接覆盖 m_seat，因此最终绑定到后注册的
 * niri-agent seat，剪贴板 data-control device 也建在它上面，
 * 导致剪贴板历史收不到 seat0 的 selection 事件（面板不更新，但粘贴正常）。
 *
 * 这里用 LD_PRELOAD 拦截 wl_proxy_add_listener，只让每个 wl_display
 * 保留第一个 wl_seat（即 seat0），把后续 seat 的 global 事件丢弃。
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

typedef void (*impl_fn)(void);

struct display_seat_state {
  struct wl_display *display;
  int seen;
  struct display_seat_state *next;
};

static struct display_seat_state *g_states = NULL;

static struct display_seat_state *state_for(struct wl_display *display) {
  for (struct display_seat_state *s = g_states; s != NULL; s = s->next) {
    if (s->display == display) {
      return s;
    }
  }
  struct display_seat_state *s = calloc(1, sizeof(*s));
  if (s == NULL) {
    return NULL;
  }
  s->display = display;
  s->next = g_states;
  g_states = s;
  return s;
}

struct registry_wrap {
  impl_fn orig_global;
  impl_fn orig_remove;
  void *user_data;
  struct wl_display *display;
};

static void wrapped_global(void *data, struct wl_registry *registry, uint32_t name,
                           const char *interface, uint32_t version) {
  struct registry_wrap *w = data;
  if (strcmp(interface, "wl_seat") == 0) {
    struct display_seat_state *st = state_for(w->display);
    if (st != NULL) {
      if (st->seen) {
        /* 丢弃第二个及以后的 wl_seat（niri-agent） */
        return;
      }
      st->seen = 1;
    }
  }
  if (w->orig_global != NULL) {
    ((void (*)(void *, struct wl_registry *, uint32_t, const char *, uint32_t))w->orig_global)(
        w->user_data, registry, name, interface, version);
  }
}

static void wrapped_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
  struct registry_wrap *w = data;
  if (w->orig_remove != NULL) {
    ((void (*)(void *, struct wl_registry *, uint32_t))w->orig_remove)(w->user_data, registry, name);
  }
}

int wl_proxy_add_listener(struct wl_proxy *proxy, void (**implementation)(void), void *data) {
  static int (*real)(struct wl_proxy *, void (**)(void), void *) = NULL;
  if (real == NULL) {
    real = (int (*)(struct wl_proxy *, void (**)(void), void *))dlsym(RTLD_NEXT, "wl_proxy_add_listener");
    if (real == NULL) {
      return -1;
    }
  }

  const struct wl_interface *iface = wl_proxy_get_interface(proxy);
  if (iface != NULL && strcmp(iface->name, "wl_registry") == 0 && implementation != NULL &&
      implementation[0] != NULL) {
    struct registry_wrap *w = calloc(1, sizeof(*w));
    if (w != NULL) {
      w->orig_global = implementation[0];
      w->orig_remove = implementation[1];
      w->user_data = data;
      w->display = wl_proxy_get_display(proxy);
      static void (*wrapped[2])(void) = {(void (*)(void))wrapped_global, (void (*)(void))wrapped_global_remove};
      return real(proxy, wrapped, w);
    }
  }
  return real(proxy, implementation, data);
}
