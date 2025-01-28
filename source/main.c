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
static int num_hourly;

// General stuff
static int print_usb;
static int scrWidth;
static int scrHeight;

// Menu stuff
enum menu_state_enum {
	NOT_IN_MENU,
	MENU_SETTINGS,
	MENU_MSGBOX
};

static enum menu_state_enum menu_state = NOT_IN_MENU;
enum setting_pos_enum {
	SETTING_CONTINUOUS_PLAY_ON_OFF,
	SETTING_ALARM_ON_OFF,
	SETTING_ALARM_TIME_HRS,
	SETTING_ALARM_TIME_MINS,
	SETTING_HOURLY_ON_OFF,
	SETTING_SHUTDOWN_AFTER_ALARM,
	SETTINGS_CANCEL,
	SETTINGS_SAVE,
	SETTING_MAX
};
static enum setting_pos_enum settings_pos = SETTING_CONTINUOUS_PLAY_ON_OFF;

static int msgBoxTimer = 0;
static char* msgBoxTitle = NULL;
static char* msgBoxMsg = NULL;

// Alarm stuff
static int continuousPlayOn = 1;
static int alarmOn = 0;
static int alarmHrs = 7;
static int alarmMins = 0;
static int hourlyAlarmOn = 0;
static int alarmGoingOff = 0;
static int hourlyGoingOff = 0;
static int shutdownAfterAlarm = 0;

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
	if(albumNum != -1) {
		sprintf(dirPath, "/wakemii/albums/%s", albums[albumNum]->name);
	}
	else {
		// Hourly
		strcpy(dirPath, "/wakemii/hourly");
	}
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
		sprintf(absPath, "/wakemii/albums/%s/cover.%s", albums[randAlbumNum]->name, coverExt);
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

static int shutdown = 0;
void ShutdownWii() {
	shutdown = 1;
}

void loadSettings() {
	FILE *fp = fopen("/wakemii/settings.cfg", "rb");
	if(!fp) {
		print_gecko("settings.cfg not found\r\n");
		return;
	}
	fseek(fp, 0L, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	if(size > 1024*1024) {
		print_gecko("settings.cfg is too large!\r\n");
		return;
	}
	char *fileContentsBuffer = calloc(1, size);
	size_t ret = fread(fileContentsBuffer, 1, size, fp);
	fclose(fp);
	if(ret != size) {
		print_gecko("settings.cfg failed to read (expected %d got %d)\r\n", size, ret);
		return;
	}
	
	// parse back to our vars
	char *line, *linectx = NULL;
	line = strtok_r( fileContentsBuffer, "\r\n", &linectx );
	while( line != NULL ) {
		//print_gecko("Line [%s]\r\n", line);
		if(line[0] != '#') {
			char *name, *value = NULL;
			name = strtok_r(line, "=", &value);
			
			if(value != NULL) {
				//print_gecko("Name [%s] Value [%s]\r\n", name, value);
				if(!strcmp("Continuous Play", name)) {
					continuousPlayOn = !strcmp("yes", value);
				}
				else if(!strcmp("Alarm On", name)) {
					alarmOn = !strcmp("yes", value);
				}
				else if(!strcmp("Hourly Alarm On", name)) {
					hourlyAlarmOn = !strcmp("yes", value);
				}
				else if(!strcmp("Shutdown after alarm", name)) {
					shutdownAfterAlarm = !strcmp("yes", value);
				}
				else if(!strcmp("Alarm Hour", name)) {
					alarmHrs = atoi(value);
				}
				else if(!strcmp("Alarm Minute", name)) {
					alarmMins = atoi(value);
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}
}

bool saveSettings() {
	char *configString = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&configString, &len);
	if(!fp) return false;

	// Write in a format we can parse later
	fprintf(fp, "# WakeMii configuration file, do not edit anything unless you know what you're doing!\r\n");
	fprintf(fp, "Continuous Play=%s\r\n", continuousPlayOn ? "yes":"no");
	fprintf(fp, "Alarm On=%s\r\n", alarmOn ? "yes":"no");
	fprintf(fp, "Alarm Hour=%02d\r\n", alarmHrs);
	fprintf(fp, "Alarm Minute=%02d\r\n", alarmMins);
	fprintf(fp, "Hourly Alarm On=%s\r\n", hourlyAlarmOn ? "yes":"no");
	fprintf(fp, "Shutdown after alarm=%s\r\n", shutdownAfterAlarm ? "yes":"no");
	fclose(fp);
	
	fp = fopen("/wakemii/settings.cfg", "wb");
	if(!fp) {
		print_gecko("settings.cfg failed to create\r\n");
		return false;
	}
	int res = fwrite(configString, 1, len, fp);
	if(res != len) {
		print_gecko("settings.cfg failed to write (expected to write %d wrote %d)\r\n", len, res);
		return false;
	}
	fclose(fp);
	return true;
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
	WPAD_SetIdleTimeout(120);
	WPAD_SetPowerButtonCallback((WPADShutdownCallback) ShutdownWii);
	SYS_SetPowerCallback(ShutdownWii);
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS);

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
	
	// Parse the wakemii album dir for valid directories.
	DIR* dp = opendir( "/wakemii/albums" );
	if(!dp) {
		print_gecko("wakemii/albums dir not found!\r\n");
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
		sprintf(absPath, "/wakemii/albums/%s", entry->d_name);
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
	
	// Parse the wakemii hourly dir for mp3 files.
	dp = opendir( "/wakemii/hourly" );
	if(!dp) {
		print_gecko("wakemii/hourly dir not found!\r\n");
		return -1;
	}

	while( (entry = readdir(dp)) != NULL ){
		if(!strcasecmp(entry->d_name, "..") || !strcasecmp(entry->d_name, ".")) {
			continue;
		}
		char absPath[1024];
		memset(absPath, 0, 1024);
		sprintf(absPath, "/wakemii/hourly/%s", entry->d_name);
		stat(absPath,&fstat);
		if(!(fstat.st_mode & _IFDIR)) {
			if(endsWith(entry->d_name, ".mp3")) {
				num_hourly++;
			}
		}
	}
	closedir(dp);
	print_gecko("Found %i hourly chimes\r\n", num_hourly);
	
	// Load settings
	loadSettings();
	if(!num_hourly && hourlyAlarmOn) {
		hourlyAlarmOn = 0;
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
	
	char entryName[1024];
	memset(entryName, 0, 1024);
	char* entryNamePtr = &entryName[0];
	
	MP3Player_Init();
	FILE *mp3File = NULL;
	if(continuousPlayOn) {
		mp3File = getEntryFromIndex(randAlbumNum, randTrackFromAlbum, entryNamePtr);
		print_gecko("mp3File ptr %08X\r\n", mp3File);
		if(mp3File != NULL) {
			MP3Player_PlayFile(mp3File, &mp3Reader, NULL);
		}
	}
	
	char timeLine[256];
	memset(timeLine, 0, 256);
	time_t curtime;	

	int change_entry = 0;
	int change_entry_rand = 0;
	int change_album = 0;
	int alarmSongHandled = 0;
	int change_entry_rand_hourly = 0;
	int hourlyChimeHandled = 0;
	
    while(1) {
		if(shutdown) {
			MP3Player_Stop();
			SYS_ResetSystem(SYS_POWEROFF, 0, 0);
		}
		// If we're not on continuous play, nop out any nav actions for songs.
		if(!continuousPlayOn && !alarmGoingOff) {
			change_entry = 0;
			change_entry_rand = 0;
			change_album = 0;
		}
		// Trigger a song for the alarm.
		if(alarmGoingOff && !alarmSongHandled) {
			alarmSongHandled = 1;
			change_entry_rand = 1;
		}
		// Trigger a song for the hourly alarm.
		if(hourlyGoingOff && !hourlyChimeHandled) {
			hourlyChimeHandled = 1;
			change_entry_rand_hourly = 1;
		}
		
		// Change in track was requested, handle it.
		if(change_entry || change_entry_rand || change_album || change_entry_rand_hourly) {
			if(change_entry_rand_hourly) {
				MP3Player_Stop();
				if(mp3File) {
					fclose(mp3File);
				}
				memset(entryName, 0, 1024);
				mp3File = getEntryFromIndex(-1, rand() % num_hourly, entryNamePtr);
				print_gecko("mp3File ptr %08X\r\n", mp3File);
				if(mp3File != NULL) {
					MP3Player_PlayFile(mp3File, &mp3Reader, NULL);
				}
				
				if(cover) {
					GRRLIB_FreeTexture(cover);
					cover = NULL;
				}
				
				change_entry_rand_hourly = 0;
			}
			else {
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
		}
		
		// Scan for input
        WPAD_ScanPads();
        const u32 wpaddown = WPAD_ButtonsDown(0);
        const u32 wpadheld = WPAD_ButtonsHeld(0);

        GRRLIB_FillScreen(GRRLIB_BLACK);    // Clear the screen
		if(mp3File && MP3Player_IsPlaying()) {
			if(cover != NULL) {
				// Draw the cover
				GRRLIB_DrawImg(coverStartX, coverStartY, cover, 0, MIN(coverScaledW, coverScaledH), MIN(coverScaledW, coverScaledH), GRRLIB_WHITE);  
			}
			if(!hourlyGoingOff) {
				GRRLIB_Printf(100, scrHeight-60, tex_BMfont5, GRRLIB_WHITE, 1, "Album: %s", albums[randAlbumNum]->name);
			}
			char *trackNameWithLabel = calloc(1, 1024);
			sprintf(trackNameWithLabel, "Track: %.*s", strlen(entryName)-4, entryName);
			GRRLIB_Printf(100, scrHeight-40, tex_BMfont5, GRRLIB_WHITE, 1, trackNameWithLabel);
			free(trackNameWithLabel);
		}
		
		// Print general stuff
		time(&curtime);
		struct tm *tmpTime = gmtime(&curtime);
		GRRLIB_Printf(50, 25, tex_BMfont3, GRRLIB_WHITE, 1, "WAKEMII");
        GRRLIB_Printf(350, 27, tex_BMfont5, GRRLIB_WHITE, 1, "Current FPS: %d | Mem Free %.2fMB", FPS, (SYS_GetArena1Hi()-SYS_GetArena1Lo())/(1048576.0f));
		if(!continuousPlayOn) {
			if(tmpTime->tm_sec % 2) {
				strftime(timeLine, sizeof(timeLine), "%H:%M", localtime(&curtime));
			}
			else {
				strftime(timeLine, sizeof(timeLine), "%H %M", localtime(&curtime));
			}
			GRRLIB_Printf(90, 150, tex_BMfont3, GRRLIB_WHITE, 3, "%s", timeLine);
		}
		else {
			strftime(timeLine, sizeof(timeLine), "%Y-%m-%d %H:%M:%S", localtime(&curtime));
			GRRLIB_Printf(350, 47, tex_BMfont5, GRRLIB_WHITE, 1, "Date Time: %s", timeLine);
		}

		// If volume was updated, set the volume
		if(vol_updated) {
			GRRLIB_Printf(500, scrHeight-60, tex_BMfont5, GRRLIB_WHITE, 1, "Volume (%i%%)", (int)(((float)vol/(float)256)*100));
			vol_updated--;
		}
		
		// Trigger the hourly alarm
		if((num_hourly && hourlyAlarmOn && !continuousPlayOn && !(alarmOn && !alarmMins)) && tmpTime->tm_min == 0 && !hourlyGoingOff) {
			print_gecko("Hourly alarm triggered!\r\n");
			hourlyGoingOff = 1;
		}
		
		// End the hourly alarm
		if((num_hourly && hourlyAlarmOn && hourlyGoingOff) && tmpTime->tm_min != 0) {
			hourlyGoingOff = 0;
			hourlyChimeHandled = 0;
		}
			
		
		// Trigger the alarm
		if(alarmOn && (alarmHrs == tmpTime->tm_hour && alarmMins == tmpTime->tm_min) && !alarmGoingOff) {
			print_gecko("Alarm triggered!\r\n");
			alarmGoingOff = 1;
		}
		
		// End the alarm
		if(alarmOn && alarmGoingOff && (alarmHrs != tmpTime->tm_hour || alarmMins != tmpTime->tm_min)) {
			alarmGoingOff = 0;
			alarmSongHandled = 0;
			if(shutdownAfterAlarm) shutdown = 1;
		}
		
		if(menu_state == MENU_SETTINGS) {
			GRRLIB_Rectangle (80, 80, 500, 300, 0x808080A0, true);
			if(settings_pos < SETTINGS_CANCEL) {
				GRRLIB_Rectangle (80, 140 + (settings_pos * 30), 12, 12, 0x808080FF, true);
			}
			GRRLIB_Printf(90, 90, tex_BMfont3, GRRLIB_WHITE, 1, "SETTINGS");
			GRRLIB_Printf(90, 140, tex_BMfont4, GRRLIB_WHITE, 1, "CONTINUOUS PLAY");
			GRRLIB_Printf(420, 140, tex_BMfont4, GRRLIB_WHITE, 1, "[%s]", continuousPlayOn ? "ON" : "OFF");
			GRRLIB_Printf(90, 170, tex_BMfont4, GRRLIB_WHITE, 1, "ALARM");
			GRRLIB_Printf(420, 170, tex_BMfont4, GRRLIB_WHITE, 1, "[%s]", alarmOn ? "ON" : "OFF");
			GRRLIB_Printf(90, 200, tex_BMfont4, GRRLIB_WHITE, 1, "ALARM HOUR");
			GRRLIB_Printf(420, 200, tex_BMfont4, GRRLIB_WHITE, 1, "%02d", alarmHrs);
			GRRLIB_Printf(90, 230, tex_BMfont4, GRRLIB_WHITE, 1, "ALARM MINUTE");
			GRRLIB_Printf(420, 230, tex_BMfont4, GRRLIB_WHITE, 1, "%02d", alarmMins);
			GRRLIB_Printf(90, 260, tex_BMfont4, GRRLIB_WHITE, 1, "HOURLY ALARM");
			GRRLIB_Printf(420, 260, tex_BMfont4, GRRLIB_WHITE, 1, "[%s]", num_hourly ? (hourlyAlarmOn ? "ON" : "OFF") : "NOT AVAIL");
			GRRLIB_Printf(90, 290, tex_BMfont4, GRRLIB_WHITE, 1, "SHUTDOWN AFTER ALARM");
			GRRLIB_Printf(420, 290, tex_BMfont4, GRRLIB_WHITE, 1, "[%s]", shutdownAfterAlarm ? "YES" : "NO");
			GRRLIB_Printf(420, 315, tex_BMfont4, GRRLIB_WHITE, 1, settings_pos == SETTINGS_CANCEL ? "(CANCEL)" : "CANCEL");
			GRRLIB_Printf(420, 345, tex_BMfont4, GRRLIB_WHITE, 1, settings_pos == SETTINGS_SAVE ? "(SAVE)" : "SAVE");
		
		}
		
		// Handle a message box
		if(menu_state == MENU_MSGBOX) {
			GRRLIB_Rectangle (80, 160, 500, 200, 0x808080A0, true);
			if(msgBoxTitle != NULL)
				GRRLIB_Printf(90, 170, tex_BMfont5, GRRLIB_WHITE, 2, "%s", msgBoxTitle);
			if(msgBoxMsg != NULL)
				GRRLIB_Printf(90, 240, tex_BMfont5, GRRLIB_WHITE, 1, "%s", msgBoxMsg);
			msgBoxTimer--;
			if(!msgBoxTimer) {
				menu_state = NOT_IN_MENU;
			}
		}

		// Handle input
		if(menu_state == NOT_IN_MENU) {
			// main screen input
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
			else if(wpaddown & WPAD_BUTTON_2) {
				menu_state = MENU_SETTINGS;
				settings_pos = 0;
			}
		}
		else if(menu_state == MENU_SETTINGS) {
			if(!num_hourly) {
				hourlyAlarmOn = 0;
			}
			// settings menu
			int oldContinuousPlayOn = continuousPlayOn;
			if(wpaddown & WPAD_BUTTON_B) {
				menu_state = NOT_IN_MENU;
			}
			else if(wpaddown & WPAD_BUTTON_UP) {
				settings_pos = (settings_pos == 0) ? SETTING_MAX-1 : settings_pos-1;
			}
			else if(wpaddown & WPAD_BUTTON_DOWN) {
				settings_pos = (settings_pos == SETTING_MAX-1) ? 0 : settings_pos+1;
			}
			else if(wpaddown & WPAD_BUTTON_RIGHT) {
				if(settings_pos == SETTING_CONTINUOUS_PLAY_ON_OFF) {
					continuousPlayOn^=1;
					if(alarmOn && continuousPlayOn) {
						alarmOn = 0;
					}
				}
				if(settings_pos == SETTING_ALARM_ON_OFF) {
					alarmOn^=1;
					if(alarmOn && continuousPlayOn) {
						continuousPlayOn = 0;
					}
				}
				if((settings_pos == SETTING_HOURLY_ON_OFF) && num_hourly) {
					hourlyAlarmOn^=1;
				}
				if(settings_pos == SETTING_ALARM_TIME_HRS) {
					alarmHrs = alarmHrs == 23 ? 0 : alarmHrs+1;
				}
				if(settings_pos == SETTING_ALARM_TIME_MINS) {
					alarmMins = alarmMins == 59 ? 0 : alarmMins+1;
				}
				if(settings_pos == SETTING_SHUTDOWN_AFTER_ALARM) {
					shutdownAfterAlarm ^= 1;
				}
			}
			else if(wpaddown & WPAD_BUTTON_LEFT) {
				if(settings_pos == SETTING_CONTINUOUS_PLAY_ON_OFF) {
					continuousPlayOn^=1;
					if(alarmOn && continuousPlayOn) {
						alarmOn = 0;
					}
				}
				if(settings_pos == SETTING_ALARM_ON_OFF) {
					alarmOn^=1;
					if(alarmOn && continuousPlayOn) {
						continuousPlayOn = 0;
					}
				}
				if((settings_pos == SETTING_HOURLY_ON_OFF) && num_hourly) {
					hourlyAlarmOn^=1;
				}
				if(settings_pos == SETTING_ALARM_TIME_HRS) {
					alarmHrs = alarmHrs == 0 ? 23 : alarmHrs-1;
				}
				if(settings_pos == SETTING_ALARM_TIME_MINS) {
					alarmMins = alarmMins == 0 ? 59 : alarmMins-1;
				}
				if(settings_pos == SETTING_SHUTDOWN_AFTER_ALARM) {
					shutdownAfterAlarm ^= 1;
				}
			}
			else if(wpaddown & WPAD_BUTTON_A) {
				if(settings_pos == SETTINGS_SAVE) {
					msgBoxTitle = "Settings";
					if(saveSettings()) {
						msgBoxMsg = "Saved Successfully";
					}
					else {
						msgBoxMsg = "Save failed!";
					}
					msgBoxTimer = 300;
					menu_state = MENU_MSGBOX;
				}
				else if(settings_pos == SETTINGS_CANCEL) {
					menu_state = NOT_IN_MENU;
				}
			}
			// If we were just playing and now we're not, handle that etc.
			if(continuousPlayOn != oldContinuousPlayOn) {
				if(oldContinuousPlayOn) {
					MP3Player_Stop();
					if(mp3File) {
						fclose(mp3File);
					}
				}
				else {
					change_entry_rand = 1;
				}
			}
			
			
		}
        //if(wpadheld & WPAD_BUTTON_1 && wpadheld & WPAD_BUTTON_2) {
        //    WPAD_Rumble(WPAD_CHAN_0, 1); // Rumble on
        //    GRRLIB_ScrShot("sd:/grrlib.png");
        //    WPAD_Rumble(WPAD_CHAN_0, 0); // Rumble off
        //}

        GRRLIB_Render();
        FPS = CalculateFrameRate();
		if(continuousPlayOn && !MP3Player_IsPlaying()) {
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
