/*
 * (C) 2014-2021 by Christian Hesse <mail@eworm.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>

#include <iniparser.h>

#include <keyutils.h>

#include <yubikey.h>
#include <ykpers-1/ykdef.h>
#include <ykpers-1/ykcore.h>

#include <libcryptsetup.h>

#include "../config.h"
#include "../version.h"

#define PROGNAME "ykfde"

/* Yubikey supports write of 64 byte challenge to slot, returns
 * HMAC-SHA1 response.
 *
 * Lengths are defined in ykpers-1/ykdef.h:
 * SHA1_MAX_BLOCK_SIZE	64
 * SHA1_DIGEST_SIZE	20
 *
 * For passphrase we use hex encoded digest, that is twice the
 * length of binary digest. */
#define CHALLENGELEN	SHA1_MAX_BLOCK_SIZE
#define RESPONSELEN	SHA1_MAX_BLOCK_SIZE
#define PASSPHRASELEN	SHA1_DIGEST_SIZE * 2
#define MAX2FLEN	CHALLENGELEN / 2

const static char optstring[] = "hn:Ns:SV";
const static struct option options_long[] = {
	/* name			has_arg			flag	val */
	{ "help",		no_argument,		NULL,	'h' },
	{ "2nd-factor",		required_argument,	NULL,	's' },
	{ "ask-2nd-factor",	no_argument,		NULL,	'S' },
	{ "new-2nd-factor",	required_argument,	NULL,	'n' },
	{ "ask-new-2nd-factor",	no_argument,		NULL,	'N' },
	{ "version",		no_argument,		NULL,	'V' },
	{ 0, 0, 0, 0 }
};

char * ask_secret(const char * text) {
	struct termios tp, tp_save;
	char * factor = NULL;
	size_t len;
	ssize_t readlen;
	bool onTerminal = false;

	/* get terminal properties */
	if (tcgetattr(STDIN_FILENO, &tp) == 0) {
		onTerminal = true;
		tp_save = tp;

		/* disable echo on terminal */
		tp.c_lflag &= ~ECHO;
		if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tp) < 0) {
			fprintf(stderr, "Failed setting terminal attributes.\n");
			return NULL;
		}

		printf("Please give %s:", text);
	}

	readlen = getline(&factor, &len, stdin);
	factor[readlen - 1] = '\0';

	if (onTerminal == true) {
		putchar('\n');

		/* restore terminal */
		if (tcsetattr(STDIN_FILENO, TCSANOW, &tp_save) < 0) {
			fprintf(stderr, "Failed to restore terminal attributes.\n");
			free(factor);
			return NULL;
		}
	}

	return factor;
}

int main(int argc, char **argv) {
	unsigned int version = 0, help = 0, challenge_int[CHALLENGELEN];
	char challenge_old[CHALLENGELEN + 1],
		challenge_new[CHALLENGELEN + 1],
		response_old[RESPONSELEN],
		response_new[RESPONSELEN],
		passphrase_old[PASSPHRASELEN + 1],
		passphrase_new[PASSPHRASELEN + 1];
	const char * tmp;
	char challengefilename[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 1],
		challengefiletmpname[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 7 /* -XXXXXX */ + 1];
	int challengefile = 0, challengefiletmp = 0;
	int i;
	size_t len;
	int8_t rc = EXIT_FAILURE;
	/* cryptsetup */
	const char * device_name;
	int8_t luks_slot = -1;
	struct crypt_device *cryptdevice;
	crypt_status_info cryptstatus;
	crypt_keyslot_info cryptkeyslot;
	char * passphrase = NULL;
	/* keyutils */
	key_serial_t key = -1;
	void * payload = NULL;
	char * second_factor = NULL, * new_2nd_factor = NULL, * new_2nd_factor_verify = NULL;
	/* yubikey */
	YK_KEY * yk;
	uint8_t yk_slot = SLOT_CHAL_HMAC2;
	unsigned int serial = 0;
	/* iniparser */
	dictionary * ini;
	char section_ykslot[10 /* unsigned int in char */ + 1 + sizeof(CONFYKSLOT) + 1];
	char section_luksslot[10 + 1 + sizeof(CONFLUKSSLOT) + 1];

	/* get command line options */
	while ((i = getopt_long(argc, argv, optstring, options_long, NULL)) != -1)
		switch (i) {
			case 'h':
				help++;
				break;
			case 'n':
			case 'N':
				if (new_2nd_factor != NULL) {
					fprintf(stderr, "We already have a new second factor. Did you specify it twice?\n");
					goto out10;
				}

				if (optarg == NULL) { /* N */
					if ((new_2nd_factor = ask_secret("new second factor")) == NULL)
						goto out10;

					if ((new_2nd_factor_verify = ask_secret("new second factor for verification")) == NULL)
						goto out10;

					if (strcmp(new_2nd_factor, new_2nd_factor_verify) != 0) {
						fprintf(stderr, "Verification failed, given strings do not match.\n");
						goto out10;
					}
				} else { /* n */
					new_2nd_factor = strdup(optarg);
					memset(optarg, '*', strlen(optarg));
				}

				break;
			case 's':
			case 'S':
				if (second_factor != NULL) {
					fprintf(stderr, "We already have a second factor. Did you specify it twice?\n");
					goto out10;
				}

				if (optarg == NULL) { /* S */
					second_factor = ask_secret("current second factor");
				} else { /* s */
					second_factor = strdup(optarg);
					memset(optarg, '*', strlen(optarg));
				}

				break;
			case 'V':
				version++;
				break;
		}

	if (version > 0)
		printf("%s: %s v%s (compiled: " __DATE__ ", " __TIME__ ")\n", argv[0], PROGNAME, VERSION);

	if (help > 0)
		fprintf(stderr, "usage: %s [-h|--help] [-n|--new-2nd-factor <new-2nd-factor>] [-N|--ask-new-2nd-factor]\n"
				"        [-s|--2nd-factor <2nd-factor>] [-S|--ask-2nd-factor] [-V|--version]\n", argv[0]);

	if (version > 0 || help > 0)
		return EXIT_SUCCESS;

	/* initialize static buffers */
	memset(challenge_int, 0, CHALLENGELEN * sizeof(unsigned int));
	memset(challenge_old, 0, CHALLENGELEN + 1);
	memset(challenge_new, 0, CHALLENGELEN + 1);
	memset(response_old, 0, RESPONSELEN);
	memset(response_new, 0, RESPONSELEN);
	memset(passphrase_old, 0, PASSPHRASELEN + 1);
	memset(passphrase_new, 0, PASSPHRASELEN + 1);

	if ((ini = iniparser_load(CONFIGFILE)) == NULL) {
		fprintf(stderr, "Could not parse configuration file.\n");
		goto out10;
	}

	if ((device_name = iniparser_getstring(ini, "general:" CONFDEVNAME, NULL)) == NULL) {
		/* read from crypttab? */
		/* get device from currently open devices? */
		fprintf(stderr, "Could not read LUKS device from configuration file.\n");
		goto out20;
	}

	/* init and open first Yubikey */
	if (yk_init() == 0) {
		perror("yk_init() failed");
		goto out20;
	}

	if ((yk = yk_open_first_key()) == NULL) {
		fprintf(stderr, "No Yubikey available.\n");
		goto out30;
	}

	/* read the serial number from key */
	if (yk_get_serial(yk, 0, 0, &serial) == 0) {
		perror("yk_get_serial() failed");
		goto out40;
	}

	/* get the yk slot */
	sprintf(section_ykslot, "%d:" CONFYKSLOT, serial);
	yk_slot = iniparser_getint(ini, "general:" CONFYKSLOT, yk_slot);
	yk_slot = iniparser_getint(ini, section_ykslot, yk_slot);
	switch (yk_slot) {
		case 1:
		case SLOT_CHAL_HMAC1:
			yk_slot = SLOT_CHAL_HMAC1;
			break;
		case 2:
		case SLOT_CHAL_HMAC2:
		default:
			yk_slot = SLOT_CHAL_HMAC2;
			break;
	}

	/* get the luks slot */
	sprintf(section_luksslot, "%d:" CONFLUKSSLOT, serial);
	luks_slot = iniparser_getint(ini, section_luksslot, luks_slot);
	if (luks_slot < 0) {
		fprintf(stderr, "Please set LUKS key slot for Yubikey with serial %d!\n"
				"Add something like this to " CONFIGFILE ":\n\n"
				"[%d]\nluks slot = 1\n", serial, serial);
		goto out40;
	}

	/* try to get a second factor */
	if (iniparser_getboolean(ini, "general:" CONF2NDFACTOR, 0) > 0 &&
			second_factor == NULL && new_2nd_factor == NULL) {
		if (sd_notify(0, "READY=0\nSTATUS=Detecting systemd...") == 0)
			fprintf(stderr, "Not running from systemd, you may have to give\n"
					"second factor manually if required.\n");
		else if ((key = keyctl_search(KEY_SPEC_USER_KEYRING, "user", "ykfde-2f", 0)) < 0)
			/* get second factor from key store */
			fprintf(stderr, "Failed requesting key. That's ok if you do not use\n"
					"second factor. Give it manually if required.\n");

		/* if we have a key id we have a key - so this should succeed */
		if (key > -1) {
			if (keyctl_read_alloc(key, &payload) < 0) {
				perror("Failed reading payload from key");
				goto out40;
			}
			second_factor = payload;
		}
	}

	/* use an empty string if second_factor is still NULL */
	if (second_factor == NULL)
		second_factor = strdup("");

	/* warn when second factor is not enabled in config */
	if (iniparser_getboolean(ini, "general:" CONF2NDFACTOR, 0) == 0 &&
			((second_factor != NULL && *second_factor != 0) ||
			 (new_2nd_factor != NULL && *new_2nd_factor != 0)))
		fprintf(stderr, "Warning: Processing second factor, but not enabled in config!\n");

	/* get random number - try random first, fall back to urandom
	   We generate an array of unsigned int, the use modulo to limit to printable
	   ASCII characters (32 to 127). */
	if ((len = getrandom(challenge_int, CHALLENGELEN * sizeof(unsigned int), GRND_RANDOM|GRND_NONBLOCK)) != CHALLENGELEN * sizeof(unsigned int))
		getrandom((void *)((size_t)challenge_int + len), CHALLENGELEN * sizeof(unsigned int) - len, 0);
	for (i = 0; i < CHALLENGELEN; i++)
		challenge_new[i] = (challenge_int[i] % (127 - 32)) + 32;

	/* these are the filenames for challenge
	 * we need this for reading and writing */
	sprintf(challengefilename, CHALLENGEDIR "/challenge-%d", serial);
	sprintf(challengefiletmpname, CHALLENGEDIR "/challenge-%d-XXXXXX", serial);

	/* write new challenge to file */
	if ((challengefiletmp = mkstemp(challengefiletmpname)) < 0) {
		fprintf(stderr, "Could not open file %s for writing.\n", challengefiletmpname);
		goto out40;
	}
	if (write(challengefiletmp, challenge_new, CHALLENGELEN) < 0) {
		fprintf(stderr, "Failed to write challenge to file.\n");
		goto out50;
	}
	if (fsync(challengefiletmp) < 0) {
		fprintf(stderr, "Failed to sync file to disk.\n");
		goto out50;
	}
	challengefiletmp = close(challengefiletmp);

	/* now that the new challenge has been written to file...
	 * add second factor to new challenge */
	tmp = new_2nd_factor ? new_2nd_factor : second_factor;
	len = strlen(tmp);
	memcpy(challenge_new, tmp, len < MAX2FLEN ? len : MAX2FLEN);

	/* do challenge/response and encode to hex */
	if (yk_challenge_response(yk, yk_slot, true,
			CHALLENGELEN, (unsigned char *) challenge_new,
			RESPONSELEN, (unsigned char *) response_new) == 0) {
		perror("yk_challenge_response() failed");
		goto out50;
	}
	yubikey_hex_encode((char *) passphrase_new, (char *) response_new, SHA1_DIGEST_SIZE);

	/* get status of crypt device
	 * We expect this to be active (or busy). It is the actual root device, no? */
	cryptstatus = crypt_status(cryptdevice, device_name);
	if (cryptstatus != CRYPT_ACTIVE && cryptstatus != CRYPT_BUSY) {
		fprintf(stderr, "Device %s is invalid or inactive.\n", device_name);
		goto out50;
	}

	/* initialize crypt device */
	if (crypt_init_by_name(&cryptdevice, device_name) < 0) {
		fprintf(stderr, "Device %s failed to initialize.\n", device_name);
		goto out60;
	}

	cryptkeyslot = crypt_keyslot_status(cryptdevice, luks_slot);

	if (cryptkeyslot == CRYPT_SLOT_INVALID) {
		fprintf(stderr, "Key slot %d is invalid.\n", luks_slot);
		goto out60;
	} else if (cryptkeyslot == CRYPT_SLOT_ACTIVE || cryptkeyslot == CRYPT_SLOT_ACTIVE_LAST) {
		/* read challenge from file */
		if ((challengefile = open(challengefilename, O_RDONLY)) < 0) {
			perror("Failed opening challenge file for reading");
			goto out60;
		}

		if (read(challengefile, challenge_old, CHALLENGELEN) < 0) {
			perror("Failed reading challenge from file");
			goto out60;
		}

		challengefile = close(challengefile);
		/* finished reading challenge */

		/* copy the second factor */
		len = strlen(second_factor);
		memcpy(challenge_old, second_factor, len < MAX2FLEN ? len : MAX2FLEN);

		/* do challenge/response and encode to hex */
		if (yk_challenge_response(yk, yk_slot, true,
				CHALLENGELEN, (unsigned char *) challenge_old,
				RESPONSELEN, (unsigned char *) response_old) == 0) {
			perror("yk_challenge_response() failed");
			goto out60;
		}
		yubikey_hex_encode((char *) passphrase_old, (char *) response_old, SHA1_DIGEST_SIZE);

		if (crypt_keyslot_change_by_passphrase(cryptdevice, luks_slot, luks_slot,
				passphrase_old, PASSPHRASELEN,
				passphrase_new, PASSPHRASELEN) < 0) {
			fprintf(stderr, "Could not update passphrase for key slot %d.\n", luks_slot);
			goto out60;
		}

		if (renameat2(AT_FDCWD, challengefiletmpname, AT_FDCWD, challengefilename, RENAME_EXCHANGE) < 0) {
			fprintf(stderr, "Failed to rename (exchange) challenge files.\n");
			goto out60;
		}

		if (unlink(challengefiletmpname) < 0) {
			fprintf(stderr, "Failed to delete old challenge file.\n");
			goto out60;
		}
	} else { /* ck == CRYPT_SLOT_INACTIVE */
		if ((passphrase = ask_secret("existing LUKS passphrase")) == NULL)
			goto out60;

		if (crypt_keyslot_add_by_passphrase(cryptdevice, luks_slot,
				passphrase, strlen(passphrase),
				passphrase_new, PASSPHRASELEN) < 0) {
			fprintf(stderr, "Could not add passphrase for key slot %d.\n", luks_slot);
			goto out60;
		}

		if (rename(challengefiletmpname, challengefilename) < 0) {
			fprintf(stderr, "Failed to rename new challenge file.\n");
			goto out60;
		}
	}

	sd_notify(0, "READY=1\nSTATUS=All done.");

	rc = EXIT_SUCCESS;

out60:
	/* free crypt context */
	crypt_free(cryptdevice);

out50:
	/* close the challenge file */
	if (challengefile)
		close(challengefile);
	if (challengefiletmp)
		close(challengefiletmp);
	if (access(challengefiletmpname, F_OK) == 0)
		unlink(challengefiletmpname);

out40:
	/* close Yubikey */
	if (yk_close_key(yk) == 0)
		perror("yk_close_key() failed");

out30:
	/* release Yubikey */
	if (yk_release() == 0)
		perror("yk_release() failed");

out20:
	/* free iniparser dictionary */
	iniparser_freedict(ini);

out10:
	/* wipe response (cleartext password!) from memory */
	/* This is statically allocated and always save to wipe! */
	memset(challenge_int, 0, CHALLENGELEN * sizeof(unsigned int));
	memset(challenge_old, 0, CHALLENGELEN + 1);
	memset(challenge_new, 0, CHALLENGELEN + 1);
	memset(response_old, 0, RESPONSELEN);
	memset(response_new, 0, RESPONSELEN);
	memset(passphrase_old, 0, PASSPHRASELEN + 1);
	memset(passphrase_new, 0, PASSPHRASELEN + 1);

	free(passphrase);
	free(new_2nd_factor_verify);
	free(new_2nd_factor);
	free(second_factor);

	return rc;
}

// vim: set syntax=c:
