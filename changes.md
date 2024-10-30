
The source code for Tk is managed by fossil.  Tk developers coordinate all
changes to the Tk source code at

> [Tk Source Code](https://core.tcl-lang.org/tk/)

Release Tk 8.7b1 arises from the check-in with tag core-8-7-b1.

Highlighted differences between Tk 8.7 and Tk 8.6 are summarized below,
with focus on changes important to programmers using the Tk library and
writing Tcl scripts containing Tk commands.

## Many improvements to use of platform features and conventions.
 - Built-in widgets and themes are scaling-aware.
 - Improved support of two-finger gestures, where available
 - The `tk windowingsystem` "aqua" needs macOS 10.9 or later

## New commands and options
 - `tk sysnotify` — Access to the OS notifications system
 - `tk systray` — Access to the OS tray facility
 - `tk print` — Access to the OS printing facility

## Widget options
 - New `ttk::progressbar` option: **-text**
 - `$frame ... -backgroundimage $img -tile $bool`
 - `$menu id`, `$menu add|insert ... ?$id? ...`
 - `$image get ... -withalpha ...`
 - All indices now accept the forms **end**, **end-int**, **int+|-int**

## Improved widget appearance
 - `ttk::notebook` with nondefault tab positions

## Images
 - Partial SVG support
 - Read/write access to photo image metadata

