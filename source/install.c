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

//patch homebrew roms if gameCode is #### or null
static bool _patchGameCode(tDSiHeader* h)
{
	if (!h) return false;

	if ((strcmp(h->ndshdr.gameCode, "####") == 0 && h->tid_low == 0x23232323) || (!*h->ndshdr.gameCode && h->tid_low == 0))
	{
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

	return false;
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

static unsigned long long _getSaveDataSize(tDSiHeader* h)
{
	unsigned long long size = 0;

	if (h)
	{
		size += h->public_sav_size;
		size += h->private_sav_size;

		//banner.sav
		if (h->appflags & 0x4)
			size += 0x4000;
	}

	return size;
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

static bool _checkDsiSpace(unsigned long long size)
{
	iprintf("Enough room on DSi?...");
	swiWaitForVBlank();

	if (getDsiFree() < size)
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

bool install(char* fpath, bool systemTitle)
{
	bool result = false;

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

	char* templatePath = "sd:/_nds/template.dsi";
	// extract template
	mkdir("/_nds", 0777);
	remove(templatePath);
	copyFile("nitro:/sdcard.nds", templatePath);
	iprintf("Template copied to SD.\n");
	FILE* template = fopen("sd:/_nds/template.dsi", "rb+");

	// hardcode the only two constants. This may be changed one day, will just release a new one at that point anyway
	u32 gamepath_location = 0x229BC;
	u8 gamepath_length = 252;

	tNDSHeader* targetheader = getRomHeaderNDS(fpath);
	tNDSBanner* targetbanner = getRomBannerNDS(fpath);
	tDSiHeader* templateheader = getRomHeader(templatePath);

	fseek(template, 0, SEEK_SET);
	fwrite(targetheader, 1, 18, template);
	fflush(template);
	
	fseek(template, templateheader->ndshdr.bannerOffset, SEEK_SET);
	fwrite(targetbanner, sizeof(tNDSBanner), 1, template);
	fflush(template);

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

	fclose(template);
	iprintf("Forwarder created.\n\n");

	// no leeks
	free(targetheader);
	free(targetbanner);
	free(templateheader);

	tDSiHeader* h = getRomHeader(templatePath);	
	if (!h)
	{
		iprintf("\x1B[31m");	//red
		iprintf("Error: ");
		iprintf("\x1B[33m");	//yellow
		iprintf("Could not open file.\n");
		iprintf("\x1B[47m");	//white
		goto error;
	}
	else
	{
		bool fixHeader = false;

		if (_patchGameCode(h))
			fixHeader = true;

		//title id must be one of these
		if (h->tid_high == 0x00030004 ||
			h->tid_high == 0x00030005 ||
			h->tid_high == 0x00030015 ||
			h->tid_high == 0x00030017)
		{}
		else
		{
			iprintf("\x1B[31m");	//red
			iprintf("Error: ");
			iprintf("\x1B[33m");	//yellow
			iprintf("This is not a DSi rom.\n");
			iprintf("\x1B[47m");	//white
			goto error;
		}

		//get install size
		iprintf("Install Size: ");
		swiWaitForVBlank();
		
		unsigned long long fileSize = getRomSize(templatePath);
		unsigned long long installSize = fileSize + _getSaveDataSize(h);

		printBytes(installSize);
		iprintf("\n");

		if (!_checkSdSpace(installSize))
			goto error;		

		//system title patch
		if (systemTitle)
		{
			iprintf("System Title Patch...");
			swiWaitForVBlank();
			h->tid_high = 0x00030015;
			iprintf("\x1B[42m");	//green
			iprintf("Done\n");
			iprintf("\x1B[47m");	//white

			fixHeader = true;
		}

		//skip nand check if system title
		if (h->tid_high != 0x00030015)
		{
			if (!_checkDsiSpace(installSize))
			{
				if (choicePrint("Install as system title?"))
				{
					h->tid_high = 0x00030015;
					fixHeader = true;
				}				
				else
				{
					if (choicePrint("Try installing anyway?") == NO)
						goto error;
				}
			}
		}
		
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

			if (choicePrint(msg) == NO)
				goto error;

			else
			{
				iprintf("\nDeleting:\n");
				deleteDir(dirPath);
				iprintf("\n");
			}
		}

		if (!_openMenuSlot())
			goto error;

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
					int result = 0;

					result = copyFilePart(templatePath, 0x3900, fileSize, appPath);

					if (result != 0)
					{
						iprintf("\x1B[31m");	//red
						iprintf("Failed\n");
						iprintf("\x1B[33m");	//yellow
						iprintf("%s\n", appPath);
						iprintf("%s\n", strerror(errno));
						iprintf("\x1B[47m");	//white

						goto error;
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
						goto error;
				}
			}
		}

		//end
		result = true;
		iprintf("\x1B[42m");	//green
		iprintf("\nInstallation complete.\n");
		iprintf("\x1B[47m");	//white
		iprintf("Back - [B]\n");
		keyWait(KEY_A | KEY_B);

		goto complete;
	}	

error:
	messagePrint("\x1B[31m\nInstallation failed.\n\x1B[47m");

complete:
	free(h);
	return result;
}
