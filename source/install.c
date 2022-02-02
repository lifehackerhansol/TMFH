#include "install.h"
#include "sav.h"
#include "main.h"
#include "message.h"
#include "maketmd.h"
#include "rom.h"
#include "storage.h"
#include <errno.h>
#include <sys/stat.h>

static bool _titleIsUsed(tDSiHeader* h)
{
	if (!h) return false;

	char path[64];
	sprintf(path, "/title/%08x/%08x/", (unsigned int)h->tid_high, (unsigned int)h->tid_low);

	return dirExists(path);
}

// randomize TID
static bool _patchGameCode(tDSiHeader* h)
{
	if (!h) return false;

	iprintf("Fixing Game Code...");
	swiWaitForVBlank();

	//set as standard app
	h->tid_high = 0x00030004;
		
	do {
		do {
			//generate a random game code
			for (int i = 0; i < 4; i++)
				h->ndshdr.gameCode[i] = 'A' + (rand() % 26);
		}
		while (h->ndshdr.gameCode[0] == 'A'); //first letter shouldn't be A

		//correct title id
		h->tid_low = ( (h->ndshdr.gameCode[0] << 24) | (h->ndshdr.gameCode[1] << 16) | (h->ndshdr.gameCode[2] << 8) | h->ndshdr.gameCode[3] );
	}
	while (_titleIsUsed(h));

	iprintf("\x1B[42m");	//green
	iprintf("Done\n");
	iprintf("\x1B[47m");	//white
	return true;
}

static bool _iqueHack(tDSiHeader* h)
{
	if (!h) return false;

	if (h->ndshdr.reserved1[8] == 0x80)
	{
		iprintf("iQue Hack...");	
		
		h->ndshdr.reserved1[8] = 0x00;

		iprintf("\x1B[42m");	//green
		iprintf("Done\n");
		iprintf("\x1B[47m");	//white
		return true;
	}

	return false;
}

static bool _checkSdSpace(unsigned long long size)
{
	iprintf("Enough room on SD card?...");
	swiWaitForVBlank();

	if (getSDCardFree() < size)
	{
		iprintf("\x1B[31m");	//red
		iprintf("No\n");
		iprintf("\x1B[47m");	//white
		return false;
	}

	iprintf("\x1B[42m");	//green
	iprintf("Yes\n");
	iprintf("\x1B[47m");	//white
	return true;
}

static bool _openMenuSlot()
{
	iprintf("Open DSi menu slot?...");
	swiWaitForVBlank();

	if (getMenuSlotsFree() <= 0)
	{
		iprintf("\x1B[31m");	//red
		iprintf("No\n");
		iprintf("\x1B[47m");	//white
		return choicePrint("Try installing anyway?");
	}

	iprintf("\x1B[42m");	//green
	iprintf("Yes\n");
	iprintf("\x1B[47m");	//white
	return true;
}

bool installError(char* error)
{
	iprintf("\x1B[31m");	//red
	iprintf("Error: ");
	iprintf("\x1B[33m");	//yellow
	iprintf("%s", error);
	iprintf("\x1B[47m");	//white
	
	messagePrint("\x1B[31m\nInstallation failed.\n\x1B[47m");
	return false;
}

static bool _generateForwarder(char* fpath, char* templatePath)
{
	// extract template
	mkdir("/_nds", 0777);
	remove(templatePath);
	copyFile("nitro:/sdcard.nds", templatePath);
	iprintf("Template copied to SD.\n");
	FILE* template = fopen("sd:/_nds/template.dsi", "rb+");

	// hardcode the only two constants. This may be changed one day, will just release a new one at that point anyway
	u32 gamepath_location = 0x229BC;
	u8 gamepath_length = 252;

	// DSiWare check
	tDSiHeader* targetDSiWareCheck = getRomHeader(fpath);
	//title id must be one of these
	if (targetDSiWareCheck->tid_high == 0x00030004 || targetDSiWareCheck->tid_high == 0x00030005 ||
		targetDSiWareCheck->tid_high == 0x00030015 || targetDSiWareCheck->tid_high == 0x00030017)
	{
		bool choice = choicePrint("This is a DSiWare title!\nYou can install directly using\nTMFH instead, for full \ncompatibility.\nInstall anyway?");
		if(!choice) {
			free(targetDSiWareCheck);
			return false;
		}
	}

	free(targetDSiWareCheck);

	tNDSHeader* targetheader = getRomHeaderNDS(fpath);
	sNDSBannerExt* targetbanner = getRomBannerNDS(fpath);
	tDSiHeader* templateheader = getRomHeader(templatePath);

	fseek(template, 0, SEEK_SET);
	fwrite(targetheader, 1, 18, template);
	fflush(template);
	
	fseek(template, templateheader->ndshdr.bannerOffset, SEEK_SET);
	switch(targetbanner->version) {
		case NDS_BANNER_VER_ORIGINAL:
			memcpy(targetbanner->titles[6], targetbanner->titles[1], 0x100);
		case NDS_BANNER_VER_ZH:
			memcpy(targetbanner->titles[7], targetbanner->titles[1], 0x100);
		case NDS_BANNER_VER_DSi:
			u16 crccheck = swiCRC16(0xFFFF, &targetbanner->dsi_icon, 0x1180);
			if(!(targetbanner->version == NDS_BANNER_VER_DSi) || !(crccheck == targetbanner->crc[3])) {
				memset(targetbanner->reserved2, 0xFF, sizeof(targetbanner->reserved2));
				memset(targetbanner->dsi_icon, 0xFF, sizeof(targetbanner->dsi_icon));
				memset(targetbanner->dsi_palette, 0xFF, sizeof(targetbanner->dsi_palette));
				memset(targetbanner->dsi_seq, 0xFF, sizeof(targetbanner->dsi_seq));
				memset(targetbanner->reserved3, 0xFF, sizeof(targetbanner->reserved3));
				targetbanner->crc[3] = 0x0000;
				targetbanner->version = NDS_BANNER_VER_ZH_KO;
			} else targetbanner->crc[3] = crccheck;
		default:
			targetbanner->crc[0] = swiCRC16(0xFFFF, &targetbanner->icon, 0x820);
			targetbanner->crc[1] = swiCRC16(0xFFFF, &targetbanner->icon, 0x920);
			targetbanner->crc[2] = swiCRC16(0xFFFF, &targetbanner->icon, 0xA20);
			break;
	}
	fwrite(targetbanner, sizeof(sNDSBannerExt), 1, template);
	fflush(template);
	free(targetbanner);

	fseek(template, gamepath_location, SEEK_SET);
	fwrite(fpath, sizeof(char), gamepath_length, template);
	fflush(template);

	u32 targettid = __builtin_bswap32(*(u32*)targetheader->gameCode);
	fseek(template, offsetof(tDSiHeader, tid_low), SEEK_SET);
	fwrite(&targettid, sizeof(targettid), 1, template);
	fflush(template);

	// need to reload header to calc crc16
	free(templateheader);
	templateheader = getRomHeader(templatePath);
	u16 templatecrc = swiCRC16(0xFFFF, &templateheader->ndshdr, 0x15E);
	fseek(template, offsetof(tNDSHeader, headerCRC16), SEEK_SET);
	fwrite(&templatecrc, sizeof(templatecrc), 1, template);

	// need to reload banner to calc crc16
	fclose(template);
	iprintf("Forwarder created.\n\n");

	// no leeks
	free(targetheader);
	free(templateheader);
	return true;
}

bool install(char* fpath, bool randomize)
{
	char* templatePath = "sd:/_nds/template.dsi";

	//confirmation message
	{
		char str[] = "Are you sure you want to install\n";
		char* msg = (char*)malloc(strlen(str) + strlen(fpath) + 8);
		sprintf(msg, "%s%s\n", str, fpath);
		
		bool choice = choiceBox(msg);
		free(msg);
		
		if (choice == NO)
			return false;
	}

	//start installation
	clearScreen(&bottomScreen);
	iprintf("Installing %s\n\n", fpath); swiWaitForVBlank();

	if (!_generateForwarder(fpath, templatePath)) {
		return installError("Failed to generate forwarder.\n");
	}

	tDSiHeader* h = getRomHeader(templatePath);	
	if (!h)
	{
		return installError("Could not open file.\n");
	}
	else
	{
		bool fixHeader = false;

		if (randomize || (strcmp(h->ndshdr.gameCode, "####") == 0 && h->tid_low == 0x23232323) || (!*h->ndshdr.gameCode && h->tid_low == 0)) {
			if (_patchGameCode(h)) fixHeader = true;
			else return installError("Failed to randomize TID.\n");
		}

		//title id must be one of these
		if (!(h->tid_high == 0x00030004 ||
			  h->tid_high == 0x00030005 ||
			  h->tid_high == 0x00030015 ||
			  h->tid_high == 0x00030017))
			return installError("This is not a DSi ROM.\n");

		//get install size
		iprintf("Install Size: ");
		swiWaitForVBlank();
		
		unsigned long long fileSize = getRomSize(templatePath);

		printBytes(fileSize);
		iprintf("\n");

		if (!_checkSdSpace(fileSize)) return installError("Not enough space on SD.\n");

		//system title patch

		if (_iqueHack(h))
			fixHeader = true;

		//create title directory /title/XXXXXXXX/XXXXXXXX
		char dirPath[32];
		mkdir("/title", 0777);

		sprintf(dirPath, "/title/%08x", (unsigned int)h->tid_high);
		mkdir(dirPath, 0777);

		sprintf(dirPath, "/title/%08x/%08x", (unsigned int)h->tid_high, (unsigned int)h->tid_low);	

		//check if title is free
		if (_titleIsUsed(h))
		{
			char msg[64];
			sprintf(msg, "Title %08x is already used.\nInstall anyway?", (unsigned int)h->tid_low);

			if (choicePrint(msg) == NO) return installError("User cancelled install.\n");

			else
			{
				iprintf("\nDeleting:\n");
				deleteDir(dirPath);
				iprintf("\n");
			}
		}

		if (!_openMenuSlot())
			return installError("Not enough icon slots available.\n");

		mkdir(dirPath, 0777);

		//content folder /title/XXXXXXXX/XXXXXXXXX/content
		{
			char contentPath[64];
			sprintf(contentPath, "%s/content", dirPath);

			mkdir(contentPath, 0777);

			//create 00000000.app
			{
				iprintf("Creating 00000000.app...");
				swiWaitForVBlank();

				char appPath[80];
				sprintf(appPath, "%s/00000000.app", contentPath);

				//copy nds file to app
				{
					int result = copyFile(templatePath, appPath);

					if (result != 0)
					{
						char* err;
						sprintf(err, "%s\n%s\n", appPath, strerror(errno));
						return installError(err);
					}

					iprintf("\x1B[42m");	//green
					iprintf("Done\n");
					iprintf("\x1B[47m");	//white
				}

				//pad out banner if it is the last part of the file
				{
					if (h->ndshdr.bannerOffset == fileSize - 0x1C00)
					{
						iprintf("Padding banner...");
						swiWaitForVBlank();

						if (padFile(appPath, 0x7C0) == false)
						{
							iprintf("\x1B[31m");	//red
							iprintf("Failed\n");
							iprintf("\x1B[47m");	//white
						}
						else
						{
							iprintf("\x1B[42m");	//green
							iprintf("Done\n");
							iprintf("\x1B[47m");	//white
						}
					}
				}

				//update header
				{
					if (fixHeader)
					{
						iprintf("Fixing header...");
						swiWaitForVBlank();

						//fix header checksum
						h->ndshdr.headerCRC16 = swiCRC16(0xFFFF, h, 0x15E);

						//fix RSA signature
						u8 buffer[20];
						swiSHA1Calc(&buffer, h, 0xE00);
						memcpy(&(h->rsa_signature[0x6C]), buffer, 20);

						FILE* f = fopen(appPath, "r+");

						if (!f)
						{
							iprintf("\x1B[31m");	//red
							iprintf("Failed\n");
							iprintf("\x1B[47m");	//white
						}
						else
						{
							fseek(f, 0, SEEK_SET);
							fwrite(h, sizeof(tDSiHeader), 1, f);

							iprintf("\x1B[42m");	//green
							iprintf("Done\n");
							iprintf("\x1B[47m");	//white
						}

						fclose(f);
					}
				}

				//make TMD
				{
					char tmdPath[80];
					sprintf(tmdPath, "%s/title.tmd", contentPath);

					if (maketmd(appPath, tmdPath) != 0)				
						return installError("Failed to generate TMD.\n");
				}
			}
		}

		//end
		iprintf("\x1B[42m");	//green
		iprintf("\nInstallation complete.\n");
		iprintf("\x1B[47m");	//white
		iprintf("Back - [B]\n");
		keyWait(KEY_A | KEY_B);
	}
	free(h);
	return true;
}
