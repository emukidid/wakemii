# README : WakeMii

# what is it?
WakeMii was a personal project inspired by a need for a calmer alarm than a recently purchased one which felt a bit too intense for my liking. It's an alarm clock app for the Wii which allows you to drop a bunch of "albums" and "hourly" MP3 files with each album also allowing cover-art. It can also be used as a niche MP3 player with features such as prev/next track/album or random play.

The alarm track is always chosen at random, as is the hourly track.


[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/8oii7kSEqC8/0.jpg)](https://youtu.be/8oii7kSEqC8)


## QUICK USAGE:
### Storage Setup
 * Place .mp3 files in the following directory structure:
    * /wakemii/albums/\<some album name>/*.mp3
    * /wakemii/albums/\<some album name>/cover.png
    * /wakemii/albums/\<another album name>/*.mp3
    * /wakemii/albums/\<some album name>/cover.jpg
    * /wakemii/hourly/*.mp3
* JPG, PNG and BMP are supported for cover art.
* Wii will use the front SD card slot only, GameCube will use GCLoader, SD2SP2, Slot A then Slot B (MMCE devices are also supported).
### Controls:
For a Wii, only a Wii remote is supported. For GameCube, only a GC controller.
|Function|Wii|GameCube|
|--|--|--|
|Next Track|D-RIGHT|D-RIGHT|
|Prev Track|D-LEFT|D-LEFT|
|Vol Up|D-UP|D-UP|
|Vol Down|D-Down|D-Down|
|Exit|Home|Start|
|Next Album|Plus|R Trigger|
|Prev Album|Minus|L Trigger|
|Settings Menu|'2' Button|Z Button|
|Random Track|'1' Button|X Button|
|Confirm|A Button|A Button|
|Cancel|B Button|B Button|

## Issues
* :warning: **Cover art must be small because GRRLIB likes to crash or just
simply not display images if they're too big, I recommend 320x240
although I've had sizes up to 800x600 work but it can be hit or miss.**
* I have only tried 128kbps MP3 files, larger bitrate files might have issues
* The alarm will only sound for a minute (or less if the track chosen is less), it cannot be cancelled/snoozed.
* There are probably bugs!

## Possible future features
* Stop using crappy/unreadable fonts
* Move away from GRRLIB usage
* Mii integration with Mii specific profiles for the hell of it
* MOD music file support
* VGMStream integration for loads more files

## Building
Have a working devKitPro & libogc2 setup, along with grrlib installed via pacman. After that, just type make and it should compile.

## CREDITS
 * [libOGC2](https://github.com/extremscorner/libogc2): [Extrems]
 * [GRRLIB](https://github.com/GRRLIB/GRRLIB)
 * Compiled using [devKitPro](https://devkitpro.org/) and "libOGC2" ([unofficial](https://github.com/extremscorner/libogc2))
 