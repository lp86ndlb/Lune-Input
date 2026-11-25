#define _GNU_SOURCE
#include <dlfcn.h>
#include <glib.h>
#include <string.h>

// Keep system upgrades and other display backends on the distribution renderer.
static void *load_factory(const char *path) {
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return NULL;
    void *(*factory)(void) = dlsym(handle, "fcitx_addon_factory_instance");
    if (factory) return factory();
    dlclose(handle);
    return NULL;
}

void *fcitx_addon_factory_instance(void) {
    const char *vendor = "/usr/lib/x86_64-linux-gnu/fcitx5/libclassicui.so";
    const char *session = g_getenv("XDG_SESSION_TYPE");
    if (g_strcmp0(session, "x11") == 0) {
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
                    gchar *renderer = g_build_filename(dir, "libclassicui-motion.so", NULL);
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
