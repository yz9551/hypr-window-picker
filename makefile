hypr-window-picker : main.c hyprland-toplevel-export-v1-client-protocol.c hyprland-toplevel-export-v1-client-protocol.h
	cc -o hypr-window-picker -lwayland-client main.c hyprland-toplevel-export-v1-client-protocol.c

hyprland-toplevel-export-v1-client-protocol.h : protocols/hyprland-toplevel-export-v1.xml
	wayland-scanner client-header protocols/hyprland-toplevel-export-v1.xml hyprland-toplevel-export-v1-client-protocol.h

hyprland-toplevel-export-v1-client-protocol.c : protocols/hyprland-toplevel-export-v1.xml
	wayland-scanner private-code protocols/hyprland-toplevel-export-v1.xml hyprland-toplevel-export-v1-client-protocol.c

clean : 
	-rm hypr-window-picker
	-rm hyprland-toplevel-export-v1-client-protocol.h
	-rm hyprland-toplevel-export-v1-client-protocol.c

