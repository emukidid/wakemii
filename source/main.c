/*===========================================
        WakeMii - A music based alarm clock for the Wii
        
		by emu_kidid in 2025
============================================*/
#include <grrlib.h>

#include <ogc/lwp_watchdog.h>   // Needed for gettime and ticks_to_millisecs
#include <stdlib.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <sys/dir.h>
#include <fat.h>
#include <stdarg.h>
#include <stdio.h>
#include <mp3player.h>

#include "BMfont1_png.h"
#include "BMfont2_png.h"
#include "BMfont3_png.h"
#include "BMfont4_png.h"
#include "BMfont5_png.h"
#include "ocean_bmf.h"
#include "frontal_bmf.h"


// RGBA Colors
#define GRRLIB_BLACK   0x000000FF
#define GRRLIB_MAROON  0x800000FF
#define GRRLIB_GREEN   0x008000FF
#define GRRLIB_OLIVE   0x808000FF
#define GRRLIB_NAVY    0x000080FF
#define GRRLIB_PURPLE  0x800080FF
#define GRRLIB_TEAL    0x008080FF
#define GRRLIB_GRAY    0x808080FF
#define GRRLIB_SILVER  0xC0C0C0FF
#define GRRLIB_RED     0xFF0000FF
#define GRRLIB_LIME    0x00FF00FF
#define GRRLIB_YELLOW  0xFFFF00FF
#define GRRLIB_BLUE    0x0000FFFF
#define GRRLIB_FUCHSIA 0xFF00FFFF
#define GRRLIB_AQUA    0x00FFFFFF
#define GRRLIB_WHITE   0xFFFFFFFF

#define MAX_ALBUMS 4096

enum cover_type_t {
	COVER_NONE,
	COVER_PNG,
	COVER_BMP,
	COVER_JPG
};

struct album {
	char* name;
	int num_entries;
	enum cover_type_t cover_type;
};

static struct album* albums[MAX_ALBUMS];
static int num_albums;

static int print_usb;
static int scrWidth;
static int scrHeight;

char* getCoverExtensionFromType(enum cover_type_t coverType) {
	switch(coverType) {
		case COVER_PNG:
			return "png";
		case COVER_JPG:
			return "jpg";
		case COVER_BMP:
			return "bmp";
		default:
			return NULL;
	}
	return NULL;
}

void print_gecko(const char* fmt, ...)
{
	if(print_usb) {
		char tempstr[2048];
		va_list arglist;
		va_start(arglist, fmt);
		vsprintf(tempstr, fmt, arglist);
		va_end(arglist);
		usb_sendbuffer_safe(1,tempstr,strlen(tempstr));
	}
}

static u8 CalculateFrameRate(void);

s32 mp3Reader(void *cbdata, void *dst, s32 size) {
	FILE *file = cbdata;
	return fread(dst, 1, size, file);
}

char *endsWith(char *str, char *end) {
	size_t len_str = strlen(str);
	size_t len_end = strlen(end);
	if(len_str < len_end)
		return NULL;
	str += len_str - len_end;
	return !strcasecmp(str, end) ? str : NULL;
}

struct album* parseDirForAlbum(char *path, char *dirName) {
	print_gecko("Attempting to parse dir %s\r\n", path);
	struct dirent *entry;
	struct stat fstat;
	struct album *newAlbum = calloc(1, sizeof(struct album));
	DIR* dp = opendir(path);
	while((entry = readdir(dp)) != NULL ) {
		if(!strcasecmp(entry->d_name, "..") || !strcasecmp(entry->d_name, ".")) {
			continue;
		}
		
		char absPath[1024];
		memset(absPath, 0, 1024);
		newAlbum->name = calloc(1, strlen(dirName)+1);
		strcpy(newAlbum->name, dirName);
		sprintf(absPath, "%s/%s", path, entry->d_name);
		stat(absPath,&fstat);
		if(!(fstat.st_mode & _IFDIR)) {
			print_gecko("Looking at file %s\r\n", absPath);
			if(endsWith(entry->d_name, ".mp3")) {
				newAlbum->num_entries++;
				print_gecko("detected usable entry %s\r\n", entry->d_name);
			}
			else if(!strcasecmp(entry->d_name, "cover.png")) {
				newAlbum->cover_type = COVER_PNG;
			}
			else if(!strcasecmp(entry->d_name, "cover.bmp")) {
				newAlbum->cover_type = COVER_BMP;
			}
			else if(!strcasecmp(entry->d_name, "cover.jpg")) {
				newAlbum->cover_type = COVER_JPG;
			}
		}
	}
	closedir(dp);
	if(newAlbum->num_entries > 0) {
		return newAlbum;
	}
	free(newAlbum);
	return NULL;
}

FILE* getEntryFromIndex(int albumNum, int entryNum, char* entryName) {
	char dirPath[1024];
	memset(dirPath, 0, 1024);
	sprintf(dirPath, "/wakemii/%s", albums[albumNum]->name);
	print_gecko("Attempting to parse dir for random entry %s %i\r\n", dirPath, entryNum);
	struct dirent *entry;
	struct stat fstat;
	DIR* dp = opendir(dirPath);
	int i = 0;
	while((entry = readdir(dp)) != NULL ) {
		if(!strcasecmp(entry->d_name, "..") || !strcasecmp(entry->d_name, ".")) {
			continue;
		}
		char absPath[1024];
		memset(absPath, 0, 1024);
		sprintf(absPath, "%s/%s", dirPath, entry->d_name);
		stat(absPath,&fstat);
		if(!(fstat.st_mode & _IFDIR)) {
			print_gecko("Looking at file %s\r\n", absPath);
			if(endsWith(entry->d_name, ".mp3")) {
				if(i == entryNum) {
					strcpy(entryName, entry->d_name);
					closedir(dp);
					return fopen(absPath, "rb");
				}
				i++;
			}
		}
	}
	closedir(dp);
	return NULL;
}

GRRLIB_texImg* getCoverFromIdx(int randAlbumNum, float* coverScaledW, float* coverScaledH, int* coverStartX, int* coverStartY) {
	char *coverExt = getCoverExtensionFromType(albums[randAlbumNum]->cover_type);
	GRRLIB_texImg* cover = NULL;
	if(coverExt != NULL) {
		print_gecko("Attempting to load the album cover\r\n");
		char absPath[1024];
		memset(absPath, 0, 1024);
		sprintf(absPath, "/wakemii/%s/cover.%s", albums[randAlbumNum]->name, coverExt);
		cover = GRRLIB_LoadTextureFromFile(absPath);
		print_gecko("cover %s ptr %08X\r\n", absPath, cover);
		if(cover != NULL) {
			print_gecko("Cover Loaded with width %i height %i\r\n", cover->w, cover->h);
			*coverScaledW = 500.0f/(float)cover->w;
			*coverScaledH = 360.0f/(float)cover->h;
			*coverStartX = (scrWidth / 2) - (int)((*coverScaledW*(float)cover->w)/2);
			*coverStartY = (scrHeight / 2) - (int)((*coverScaledH*(float)cover->h)/2);
			print_gecko("Cover scaled width %.2f height %.2f\r\n", *coverScaledW, *coverScaledH);
		}
	}
	return cover;
}

int main() {
	
	if(usb_isgeckoalive(1)) {
		usb_flush(1);
		print_usb = 1;
	}
	print_gecko("WakeMii\r\n");
	print_gecko("Arena Size: %iKb\r\n",(SYS_GetArena1Hi()-SYS_GetArena1Lo())/1024);
	
    u8 FPS = 0; 
	int vol = 192, vol_updated = 0;

    GRRLIB_Init();
	GXRModeObj* videoMode = VIDEO_GetPreferredMode(NULL);
	scrWidth = videoMode->viWidth;
	scrHeight = videoMode->viHeight;

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);

    GRRLIB_bytemapFont *bmf_Font1 = GRRLIB_LoadBMF(ocean_bmf);
    GRRLIB_bytemapFont *bmf_Font2 = GRRLIB_LoadBMF(frontal_bmf);

    GRRLIB_texImg *tex_BMfont1 = GRRLIB_LoadTexture(BMfont1_png);
    GRRLIB_InitTileSet(tex_BMfont1, 32, 32, 32);

    GRRLIB_texImg *tex_BMfont2 = GRRLIB_LoadTexture(BMfont2_png);
    GRRLIB_InitTileSet(tex_BMfont2, 16, 16, 32);

    GRRLIB_texImg *tex_BMfont3 = GRRLIB_LoadTexture(BMfont3_png);
    GRRLIB_InitTileSet(tex_BMfont3, 32, 32, 32);

    GRRLIB_texImg *tex_BMfont4 = GRRLIB_LoadTexture(BMfont4_png);
    GRRLIB_InitTileSet(tex_BMfont4, 16, 16, 32);

    GRRLIB_texImg *tex_BMfont5 = GRRLIB_LoadTexture(BMfont5_png);
    GRRLIB_InitTileSet(tex_BMfont5, 8, 16, 0);
	
	// Parse the wakemii dir for valid directories.
	DIR* dp = opendir( "/wakemii" );
	if(!dp) {
		print_gecko("wakemii dir not found!\r\n");
		return -1;
	}
	struct dirent *entry;
	struct stat fstat;
	while( (entry = readdir(dp)) != NULL ){
		if(!strcasecmp(entry->d_name, "..") || !strcasecmp(entry->d_name, ".")) {
			continue;
		}
		char absPath[1024];
		memset(absPath, 0, 1024);
		sprintf(absPath, "/wakemii/%s", entry->d_name);
		stat(absPath,&fstat);
		if(fstat.st_mode & _IFDIR) {
			struct album* ret = parseDirForAlbum(absPath, entry->d_name);
			if(ret != NULL) {
				albums[num_albums] = ret;
				num_albums++;
			}
		}
	}
	closedir(dp);
	
	// Print all album info
	print_gecko("Found %i albums\r\n", num_albums);
	for(int i = 0; i < num_albums; i++) {
		print_gecko("Album: %s. %i tracks. Cover Type: %i\r\n", albums[i]->name, albums[i]->num_entries, albums[i]->cover_type);
	}
	
	// Pick a random album + display its artwork
	srand(gettick());
	int randAlbumNum = rand() % num_albums;
	int randTrackFromAlbum = rand() % albums[randAlbumNum]->num_entries;
	
	float coverScaledW = 1.0f;
	float coverScaledH = 1.0f;
	int coverStartX = 0;
	int coverStartY = 0;
	GRRLIB_texImg* cover = getCoverFromIdx(randAlbumNum, &coverScaledW, &coverScaledH, &coverStartX, &coverStartY);
		
	//print_gecko("Attempting to load a file from disk\r\n");
	//GRRLIB_texImg* coverTest = GRRLIB_LoadTextureFromFile("/wakemii/F-Zero/cover.jpg");
	//print_gecko("CoverTest ptr %08X\r\n", coverTest);
	
	char entryName[1024];
	memset(entryName, 0, 1024);
	char* entryNamePtr = &entryName[0];
	
	MP3Player_Init();
	FILE *mp3File = getEntryFromIndex(randAlbumNum, randTrackFromAlbum, entryNamePtr);
	print_gecko("mp3File ptr %08X\r\n", mp3File);
	if(mp3File != NULL) {
		MP3Player_PlayFile(mp3File, &mp3Reader, NULL);
	}
	
	char timeLine[256];
	memset(timeLine, 0, 256);
	time_t curtime;	

	int change_entry = 0;
	int change_entry_rand = 0;
	int change_album = 0;

    while(1) {
		if(change_entry || change_entry_rand || change_album) {
			// determine new album/track
			int prevRandAlbumNum = randAlbumNum;
			if(change_entry_rand) {
				// Random
				randAlbumNum = rand() % num_albums;
				randTrackFromAlbum = rand() % albums[randAlbumNum]->num_entries;
			}
			else {
				if(change_album) {
					// Sequential movement amongst albums, always going to the first entry in the destination album
					if(randAlbumNum + change_album < 0) {
						randAlbumNum = num_albums-1;
					}
					else if (randAlbumNum + change_album > num_albums-1) {
						randAlbumNum = 0;
					}
					else {
						randAlbumNum += change_album;
					}
					// Enter the album at the start
					randTrackFromAlbum = 0;
				}
				else if(change_entry) {
					// Sequential movement amongst entries, potentially moving into other albums
					if(randTrackFromAlbum + change_entry < 0 || randTrackFromAlbum + change_entry > albums[randAlbumNum]->num_entries-1) {
						// Go to the next/prev album cause we've reached the end of this one
						if(randAlbumNum + change_entry < 0) {
							randAlbumNum = num_albums-1;
						}
						else if (randAlbumNum + change_entry > num_albums-1) {
							randAlbumNum = 0;
						}
						else {
							randAlbumNum += change_entry;
						}
						// Enter the album at the start or end depending which direction we're going.
						if(change_entry < 0) randTrackFromAlbum = albums[randAlbumNum]->num_entries-1;
						else randTrackFromAlbum = 0;
					}
					else {
						randTrackFromAlbum += change_entry;
					}
				}
			}				
			
			// Only fetch the cover if the album changed
			if(prevRandAlbumNum != randAlbumNum) {
				if(cover) {
					GRRLIB_FreeTexture(cover);
				}
				cover = getCoverFromIdx(randAlbumNum, &coverScaledW, &coverScaledH, &coverStartX, &coverStartY);
			}
			
			MP3Player_Stop();
			if(mp3File) {
				fclose(mp3File);
			}
			memset(entryName, 0, 1024);
			mp3File = getEntryFromIndex(randAlbumNum, randTrackFromAlbum, entryNamePtr);
			print_gecko("mp3File ptr %08X\r\n", mp3File);
			if(mp3File != NULL) {
				MP3Player_PlayFile(mp3File, &mp3Reader, NULL);
			}
			change_entry = 0;
			change_entry_rand = 0;
			change_album = 0;
		}
        WPAD_ScanPads();
        const u32 wpaddown = WPAD_ButtonsDown(0);
        const u32 wpadheld = WPAD_ButtonsHeld(0);

        GRRLIB_FillScreen(GRRLIB_BLACK);    // Clear the screen
		if(cover != NULL) {
			GRRLIB_DrawImg(coverStartX, coverStartY, cover, 0, MIN(coverScaledW, coverScaledH), MIN(coverScaledW, coverScaledH), GRRLIB_WHITE);  // Draw the cover
		}
		GRRLIB_Printf(50, 25, tex_BMfont3, GRRLIB_WHITE, 1, "WAKEMII");
		
		// Print stuff
		time(&curtime);
		strftime(timeLine, sizeof(timeLine), "%Y-%m-%d %H:%M:%S", localtime(&curtime));
        GRRLIB_Printf(350, 27, tex_BMfont5, GRRLIB_WHITE, 1, "Current FPS: %d | Mem Free %.2fMB", FPS, (SYS_GetArena1Hi()-SYS_GetArena1Lo())/(1048576.0f));
		GRRLIB_Printf(350, 47, tex_BMfont5, GRRLIB_WHITE, 1, "Date Time: %s", timeLine);

		GRRLIB_Printf(100, scrHeight-60, tex_BMfont5, GRRLIB_WHITE, 1, "Album: %s", albums[randAlbumNum]->name);
		GRRLIB_Printf(100, scrHeight-40, tex_BMfont5, GRRLIB_WHITE, 1, "Track: %s", entryName);
		// If volume was updated, set the volume
		if(vol_updated) {
			GRRLIB_Printf(500, scrHeight-60, tex_BMfont5, GRRLIB_WHITE, 1, "Volume (%i%%)", (int)(((float)vol/(float)256)*100));
			vol_updated--;
		}

        if(wpaddown & WPAD_BUTTON_HOME) {
            break;
        }
        if(wpaddown & WPAD_BUTTON_LEFT) {
			change_entry = -1;
        }
        else if(wpaddown & WPAD_BUTTON_RIGHT) {
			change_entry = 1;
        }
        else if(wpadheld & WPAD_BUTTON_UP) {
			if(vol<256) {vol++; MP3Player_Volume(vol);}
			vol_updated = 300;	// ~5 sec volume display
        }
        else if(wpadheld & WPAD_BUTTON_DOWN) {
			if(vol>0) {vol--; MP3Player_Volume(vol);}
			vol_updated = 300;	// ~5 sec volume display
        }
        else if(wpaddown & WPAD_BUTTON_MINUS) {
			change_album = -1;
        }
        else if(wpaddown & WPAD_BUTTON_PLUS) {
			change_album = 1;
        }
		else if(wpaddown & WPAD_BUTTON_1) {
			change_entry_rand = 1;
        }
        //if(wpadheld & WPAD_BUTTON_1 && wpadheld & WPAD_BUTTON_2) {
        //    WPAD_Rumble(WPAD_CHAN_0, 1); // Rumble on
        //    GRRLIB_ScrShot("sd:/grrlib.png");
        //    WPAD_Rumble(WPAD_CHAN_0, 0); // Rumble off
        //}

        GRRLIB_Render();
        FPS = CalculateFrameRate();
		if(!MP3Player_IsPlaying()) {
			change_entry = 1;
		}
    }
    // Free some textures
    GRRLIB_FreeTexture(tex_BMfont1);
    GRRLIB_FreeTexture(tex_BMfont2);
    GRRLIB_FreeTexture(tex_BMfont3);
    GRRLIB_FreeTexture(tex_BMfont4);
    GRRLIB_FreeTexture(tex_BMfont5);
    GRRLIB_FreeBMF(bmf_Font1);
    GRRLIB_FreeBMF(bmf_Font2);
    GRRLIB_Exit(); // Be a good boy, clear the memory allocated by GRRLIB
    return 0;
}

/**
 * This function calculates the number of frames we render each second.
 * @return The number of frames per second.
 */
static u8 CalculateFrameRate(void) {
    static u8 frameCount = 0;
    static u32 lastTime;
    static u8 FPS = 0;
    const u32 currentTime = ticks_to_millisecs(gettime());

    frameCount++;
    if(currentTime - lastTime > 1000) {
        lastTime = currentTime;
        FPS = frameCount;
        frameCount = 0;
    }
    return FPS;
}
