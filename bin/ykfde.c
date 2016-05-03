/*
 * (C) 2014-2016 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * compile with:
 * $ gcc -o ykfde ykfde.c -lcryptsetup -liniparser -lkeyutils -lykpers-1 -lyubikey
 */

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>

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

const static char optstring[] = "hn:s:V";
const static struct option options_long[] = {
	/* name			has_arg			flag	val */
	{ "help",		no_argument,		NULL,	'h' },
	{ "2nd-factor",		required_argument,	NULL,	's' },
	{ "new-2nd-factor",	required_argument,	NULL,	'n' },
	{ "version",		no_argument,		NULL,	'V' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv) {
	unsigned int version = 0, help = 0;
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
	struct timeval tv;
	int i;
	int8_t rc = EXIT_FAILURE;
	/* cryptsetup */
	const char * device_name;
	int8_t luks_slot = -1;
	struct crypt_device *cryptdevice;
	crypt_status_info cryptstatus;
	crypt_keyslot_info cryptkeyslot;
	/* keyutils */
	key_serial_t key;
	void * payload = NULL;
	char * new_2nd_factor = NULL;
	size_t plen = 0;
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
				new_2nd_factor = strdup(optarg);
				memset(optarg, '*', strlen(optarg));
				break;
			case 's':
				payload = strdup(optarg);
				memset(optarg, '*', strlen(optarg));
				break;
			case 'V':
				version++;
				break;
		}

	if (version > 0)
		printf("%s: %s v%s (compiled: " __DATE__ ", " __TIME__ ")\n", argv[0], PROGNAME, VERSION);

	if (help > 0)
		fprintf(stderr, "usage: %s [-h] [-n <new-2nd-factor>] [-s <2nd-factor>] [-V]\n", argv[0]);

	if (version > 0 || help > 0)
		return EXIT_SUCCESS;


	/* initialize random seed */
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec * tv.tv_sec);

	/* initialize static buffers */
	memset(challenge_old, 0, CHALLENGELEN + 1);
	memset(challenge_new, 0, CHALLENGELEN + 1);
	memset(response_old, 0, RESPONSELEN);
	memset(response_new, 0, RESPONSELEN);
	memset(passphrase_old, 0, PASSPHRASELEN + 1);
	memset(passphrase_new, 0, PASSPHRASELEN + 1);

	if ((ini = iniparser_load(CONFIGFILE)) == NULL) {
		rc = EXIT_FAILURE;
		fprintf(stderr, "Could not parse configuration file.\n");
		goto out10;
	}

	if ((device_name = iniparser_getstring(ini, "general:" CONFDEVNAME, NULL)) == NULL) {
		rc = EXIT_FAILURE;
		/* read from crypttab? */
		/* get device from currently open devices? */
		fprintf(stderr, "Could not read LUKS device from configuration file.\n");
		goto out20;
	}

	/* init and open first Yubikey */
	if ((rc = yk_init()) < 0) {
		perror("yk_init() failed");
		goto out20;
	}

	if ((yk = yk_open_first_key()) == NULL) {
		rc = EXIT_FAILURE;
		fprintf(stderr, "No Yubikey available.\n");
		goto out30;
	}

	/* read the serial number from key */
	if ((rc = yk_get_serial(yk, 0, 0, &serial)) < 0) {
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
		rc = EXIT_FAILURE;
		fprintf(stderr, "Please set LUKS key slot for Yubikey with serial %d!\n"
				"Add something like this to " CONFIGFILE ":\n\n"
				"[%d]\nluks slot = 1\n", serial, serial);
		goto out40;
	}

	if (payload == NULL) {
		/* get second factor from key store */
		if ((key = request_key("user", "ykfde-2f", NULL, 0)) < 0)
			fprintf(stderr, "Failed requesting key. That's ok if you do not use\n"
					"second factor. Give it manually if required.\n");

		if (key > -1) {
			/* if we have a key id we have a key - so this should succeed */
			if ((rc = keyctl_read_alloc(key, &payload)) < 0) {
				perror("Failed reading payload from key");
				goto out40;
			}
		} else
			payload = strdup("");
	}

	/* warn when second factor is not enabled in config */
	if ((*(char*)payload != 0 || new_2nd_factor != NULL) &&
			iniparser_getboolean(ini, "general:" CONF2NDFACTOR, 0) == 0)
		fprintf(stderr, "Warning: Processing second factor, but not enabled in config!\n");

	/* get random number and limit to printable ASCII character (32 to 126) */
	for(i = 0; i < CHALLENGELEN; i++)
		challenge_new[i] = (rand() % (126 - 32)) + 32;

	/* these are the filenames for challenge
	 * we need this for reading and writing */
	sprintf(challengefilename, CHALLENGEDIR "/challenge-%d", serial);
	sprintf(challengefiletmpname, CHALLENGEDIR "/challenge-%d-XXXXXX", serial);

	/* write new challenge to file */
	if ((rc = challengefiletmp = mkstemp(challengefiletmpname)) < 0) {
		fprintf(stderr, "Could not open file %s for writing.\n", challengefiletmpname);
		goto out40;
	}
	if ((rc = write(challengefiletmp, challenge_new, CHALLENGELEN)) < 0) {
		fprintf(stderr, "Failed to write challenge to file.\n");
		goto out50;
	}
	challengefiletmp = close(challengefiletmp);

	/* now that the new challenge has been written to file...
	 * add second factor to new challenge */
	tmp = new_2nd_factor ? new_2nd_factor : payload;
	plen = strlen(tmp);
	memcpy(challenge_new, tmp, plen < MAX2FLEN ? plen : MAX2FLEN);

	/* do challenge/response and encode to hex */
	if ((rc = yk_challenge_response(yk, yk_slot, true,
				CHALLENGELEN, (unsigned char *) challenge_new,
				RESPONSELEN, (unsigned char *) response_new)) < 0) {
		perror("yk_challenge_response() failed");
		goto out50;
	}
	yubikey_hex_encode((char *) passphrase_new, (char *) response_new, SHA1_DIGEST_SIZE);

	/* get status of crypt device
	 * We expect this to be active (or busy). It is the actual root device, no? */
	cryptstatus = crypt_status(cryptdevice, device_name);
	if (cryptstatus != CRYPT_ACTIVE && cryptstatus != CRYPT_BUSY) {
		rc = EXIT_FAILURE;
                fprintf(stderr, "Device %s is invalid or inactive.\n", device_name);
		goto out60;
	}

	/* initialize crypt device */
	if ((rc = crypt_init_by_name(&cryptdevice, device_name)) < 0) {
		fprintf(stderr, "Device %s failed to initialize.\n", device_name);
		goto out60;
	}

	cryptkeyslot = crypt_keyslot_status(cryptdevice, luks_slot);

	if (cryptkeyslot == CRYPT_SLOT_INVALID) {
		rc = EXIT_FAILURE;
		fprintf(stderr, "Key slot %d is invalid.\n", luks_slot);
		goto out60;
	} else if (cryptkeyslot == CRYPT_SLOT_ACTIVE || cryptkeyslot == CRYPT_SLOT_ACTIVE_LAST) {
		/* read challenge from file */
		if ((rc = challengefile = open(challengefilename, O_RDONLY)) < 0) {
			perror("Failed opening challenge file for reading");
			goto out60;
		}

		if ((rc = read(challengefile, challenge_old, CHALLENGELEN)) < 0) {
			perror("Failed reading challenge from file");
			goto out60;
		}

		challengefile = close(challengefile);
		/* finished reading challenge */

		/* copy the second factor */
		plen = strlen(payload);
		memcpy(challenge_old, payload, plen < MAX2FLEN ? plen : MAX2FLEN);

		/* do challenge/response and encode to hex */
		if ((rc = yk_challenge_response(yk, yk_slot, true,
					CHALLENGELEN, (unsigned char *) challenge_old,
					RESPONSELEN, (unsigned char *) response_old)) < 0) {
			perror("yk_challenge_response() failed");
			goto out60;
		}
		yubikey_hex_encode((char *) passphrase_old, (char *) response_old, SHA1_DIGEST_SIZE);

		if ((rc = crypt_keyslot_change_by_passphrase(cryptdevice, luks_slot, luks_slot,
				passphrase_old, PASSPHRASELEN,
				passphrase_new, PASSPHRASELEN)) < 0) {
			fprintf(stderr, "Could not update passphrase for key slot %d.\n", luks_slot);
			goto out60;
		}

		if ((rc = unlink(challengefilename)) < 0) {
			fprintf(stderr, "Failed to delete old challenge file.\n");
			goto out60;
		}
	} else { /* ck == CRYPT_SLOT_INACTIVE */
		if ((rc = crypt_keyslot_add_by_passphrase(cryptdevice, luks_slot, NULL, 0,
				passphrase_new, PASSPHRASELEN)) < 0) {
			fprintf(stderr, "Could not add passphrase for key slot %d.\n", luks_slot);
			goto out60;
		}
	}

	if ((rc = rename(challengefiletmpname, challengefilename)) < 0) {
		fprintf(stderr, "Failed to rename new challenge file.\n");
		goto out60;
	}

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
	if (!yk_close_key(yk))
		perror("yk_close_key() failed");

out30:
	/* release Yubikey */
	if (!yk_release())
		perror("yk_release() failed");

out20:
	/* free iniparser dictionary */
	iniparser_freedict(ini);

out10:
	/* wipe response (cleartext password!) from memory */
	/* This is statically allocated and always save to wipe! */
	memset(challenge_old, 0, CHALLENGELEN + 1);
	memset(challenge_new, 0, CHALLENGELEN + 1);
	memset(response_old, 0, RESPONSELEN);
	memset(response_new, 0, RESPONSELEN);
	memset(passphrase_old, 0, PASSPHRASELEN + 1);
	memset(passphrase_new, 0, PASSPHRASELEN + 1);

	free(new_2nd_factor);
	free(payload);

	return rc;
}

// vim: set syntax=c:
