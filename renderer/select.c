#define _GNU_SOURCE
#include <dlfcn.h>
#include <glib.h>
#include <string.h>

#ifndef VENDOR_PATH
#define VENDOR_PATH "/usr/lib/x86_64-linux-gnu/fcitx5/libclassicui.so"
#define VARIANT_FILE "libclassicui-motion.so"
#define X11_ONLY 1
#define ADDON_SCOPE RTLD_LOCAL
#else
#define X11_ONLY 0
// fcitx5-rime declares Library=export:librime for loadable Rime modules.
#define ADDON_SCOPE RTLD_GLOBAL
#endif

// Fall back to the matching distribution addon after a package upgrade.
static void *load_factory(const char *path) {
    void *handle = dlopen(path, RTLD_NOW | ADDON_SCOPE);
    if (!handle) return NULL;
    void *(*factory)(void) = dlsym(handle, "fcitx_addon_factory_instance");
    if (factory) return factory();
    dlclose(handle);
    return NULL;
}

void *fcitx_addon_factory_instance(void) {
    const char *vendor = VENDOR_PATH;
    const char *session = g_getenv("XDG_SESSION_TYPE");
    if (!X11_ONLY || g_strcmp0(session, "x11") == 0) {
        gchar *data = NULL;
        gsize size = 0;
        if (g_file_get_contents(vendor, &data, &size, NULL)) {
            gchar *digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)data, size);
            g_free(data);
            gboolean matches = g_strcmp0(digest, VENDOR_DIGEST) == 0;
            g_free(digest);
            if (matches) {
                Dl_info info;
                if (dladdr((void *)&load_factory, &info)) {
                    gchar *dir = g_path_get_dirname(info.dli_fname);
                    gchar *renderer = g_build_filename(dir, VARIANT_FILE, NULL);
                    void *factory = load_factory(renderer);
                    g_free(renderer);
                    g_free(dir);
                    if (factory) return factory;
                }
            }
        }
    }
    return load_factory(vendor);
}
