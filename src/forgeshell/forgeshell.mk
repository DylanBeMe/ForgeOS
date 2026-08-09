################################################################################
#
# ForgeShell
#
################################################################################

FORGESHELL_VERSION = 0.6.4
FORGESHELL_SITE = $(TOPDIR)/package/miyoo/forgeshell/src
FORGESHELL_SITE_METHOD = local
FORGESHELL_DEPENDENCIES = sdl sdl_ttf
FORGESHELL_LICENSE = MIT
FORGESHELL_LICENSE_FILES = LICENSE

define FORGESHELL_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) clean \
		SDL_CONFIG="$(STAGING_DIR)/usr/bin/sdl-config"
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -Os" \
		CPPFLAGS='-DFORGESHELL_DEFAULT_PROFILE=\"/mnt/forgeshell/device.ini\"' \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		SDL_CONFIG="$(STAGING_DIR)/usr/bin/sdl-config"
endef

define FORGESHELL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/forgeshell $(TARGET_DIR)/usr/bin/forgeshell
endef

$(eval $(generic-package))
