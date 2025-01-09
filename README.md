This program captures an image of an window using the hyprland_toplevel_export_v1 protocol. Currently, the only supported image format is ppm (Netpbm image format).


# Features
[ ] transparency in images

# Usage
```sh
hypr-window-picker <window-address-in-hex> <output-image-location>
```
**Warning: the output image location is not checked for existence and WILL overwrite the file there**
get the window address by `hyprctl clients`, it is a hexadecimal string such as `55e6036b52e0`.

# Compile and Installation
```sh
make
# then copy hypr-window-picker to /usr/bin or somewhere else on the path
```
