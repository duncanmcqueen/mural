#ifndef PortalHtml_h
#define PortalHtml_h

// Extra captive portal entry for running Mural off its own hotspot instead of a WiFi
// network. Adapted from upstream nikivanov/mural (commit e284fbd, "standalone ap mode").
extern const char OFFLINE_MENU_HTML[];

// Response to POST /offline and POST /hotspot, shown while Mural restarts in hotspot mode
extern const char OFFLINE_PAGE_HTML[];

#endif
