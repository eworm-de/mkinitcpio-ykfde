/*
 * (C) 2014 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * compile with:
 * $ gcc -o ykfde ykfde.c -lykpers-1 -lyubikey -lcryptsetup -liniparser
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <iniparser.h>

#include <yubikey.h>
#include <ykpers-1/ykdef.h>
#include <ykpers-1/ykcore.h>

#include <libcryptsetup.h>

/* challenge is 64 byte,
 * HMAC-SHA1 response is 40 byte */
#define CHALLENGELEN	64
#define RESPONSELEN	SHA1_MAX_BLOCK_SIZE
#define PASSPHRASELEN	SHA1_DIGEST_SIZE * 2

#define	CONFIGFILE	"/etc/ykfde.conf"
#define CHALLENGEDIR	"/etc/ykfde.d/"
#define CONFYKSLOT	"yk slot"
#define CONFLUKSSLOT	"luks slot"
#define CONFDEVNAME	"device name"

int main(int argc, char **argv) {
	char challenge_old[CHALLENGELEN + 1],
		challenge_new[CHALLENGELEN + 1],
		repose_old[RESPONSELEN],
		repose_new[RESPONSELEN],
		passphrase_old[PASSPHRASELEN + 1],
		passphrase_new[PASSPHRASELEN + 1];
	char challengefilename[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 1],
		challengefiletmpname[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 7 /* -XXXXXX */ + 1];
	int challengefile = 0, challengefiletmp = 0;
	struct timeval tv;
	int i;
	int8_t rc = EXIT_FAILURE;
	/* cryptsetup */
	char * device_name;
	int8_t luks_slot = -1;
	struct crypt_device *cd;
	crypt_status_info cs;
	crypt_keyslot_info ck;
	/* yubikey */
	YK_KEY * yk;
	uint8_t yk_slot = SLOT_CHAL_HMAC2;
	unsigned int serial = 0;
	/* iniparser */
	dictionary * ini;
	char section_ykslot[10 /* unsigned int in char */ + 1 + sizeof(CONFYKSLOT) + 1];
	char section_luksslot[10 /* unsigned int in char */ + 1 + sizeof(CONFLUKSSLOT) + 1];

	/* initialize random seed */
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec * tv.tv_sec);

	memset(challenge_old, 0, CHALLENGELEN + 1);
	memset(challenge_new, 0, CHALLENGELEN + 1);
	memset(repose_old, 0, RESPONSELEN);
	memset(repose_new, 0, RESPONSELEN);
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
		perror("yk_open_first_key() failed");
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
		fprintf(stderr, "Please set LUKS key slot for Yubikey with serial %d!\n", serial);
		printf("Add something like this to " CONFIGFILE ":\n\n[%d]\nluks slot = 1\n", serial);
		goto out40;
	}

	/* get random number and limit to printable ASCII character (32 to 126) */
	for(i = 0; i < CHALLENGELEN; i++)
		challenge_new[i] = (rand() % (126 - 32)) + 32;

	/* do challenge/response and encode to hex */
	if ((rc = yk_challenge_response(yk, yk_slot, true,
				CHALLENGELEN, (unsigned char *)challenge_new,
				RESPONSELEN, (unsigned char *)repose_new)) < 0) {
		perror("yk_challenge_response() failed");
		goto out40;
	}
	yubikey_hex_encode((char *)passphrase_new, (char *)repose_new, 20);

	/* initialize crypt device */
	if ((rc = crypt_init_by_name(&cd, device_name)) < 0) {
		fprintf(stderr, "Device %s failed to initialize.\n", device_name);
		goto out40;
	}

	/* these are the filenames for challenge
	 * we need this for reading and writing */
	sprintf(challengefilename, CHALLENGEDIR "/challenge-%d", serial);
	sprintf(challengefiletmpname, CHALLENGEDIR "/challenge-%d-XXXXXX", serial);

	/* write new challenge to file */
	if ((rc = challengefiletmp = mkstemp(challengefiletmpname)) < 0) {
		fprintf(stderr, "Could not open file %s for writing.\n", challengefiletmpname);
		goto out50;
	}
	if ((rc = write(challengefiletmp, challenge_new, CHALLENGELEN)) < 0) {
		fprintf(stderr, "Failed to write challenge to file.\n");
		goto out60;
	}
	challengefiletmp = close(challengefiletmp);

	/* get status of crypt device
	 * We expect this to be active (or busy). It is the actual root device, no? */
	cs = crypt_status(cd, device_name);
	if (cs != CRYPT_ACTIVE && cs != CRYPT_BUSY) {
		rc = EXIT_FAILURE;
                fprintf(stderr, "Device %s is invalid or inactive.\n", device_name);
		goto out60;
	}

	ck = crypt_keyslot_status(cd, luks_slot);
	if (ck == CRYPT_SLOT_INVALID) {
		rc = EXIT_FAILURE;
		fprintf(stderr, "Key slot %d is invalid.\n", luks_slot);
		goto out60;
	} else if (ck == CRYPT_SLOT_ACTIVE || ck == CRYPT_SLOT_ACTIVE_LAST) {
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

		/* do challenge/response and encode to hex */
		if ((rc = yk_challenge_response(yk, yk_slot, true,
					CHALLENGELEN, (unsigned char *)challenge_old,
					RESPONSELEN, (unsigned char *)repose_old)) < 0) {
			perror("yk_challenge_response() failed");
			goto out60;
		}
		yubikey_hex_encode((char *)passphrase_old, (char *)repose_old, 20);

		if ((rc = crypt_keyslot_change_by_passphrase(cd, luks_slot, luks_slot,
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
		if ((rc = crypt_keyslot_add_by_passphrase(cd, luks_slot, NULL, 0,
				passphrase_new, PASSPHRASELEN)) < 0) {
			fprintf(stderr, "Could add passphrase for key slot %d.\n", luks_slot);
			goto out60;
		}
	}

	if ((rc = rename(challengefiletmpname, challengefilename)) < 0) {
		fprintf(stderr, "Failed to rename new challenge file.\n");
		goto out60;
	}

	rc = EXIT_SUCCESS;

out60:
	/* close the challenge file */
	if (challengefile)
		close(challengefile);
	if (challengefiletmp)
		close(challengefiletmp);
	if (access(challengefiletmpname, F_OK) == 0 )
		unlink(challengefiletmpname);

out50:
	/* free crypt context */
	crypt_free(cd);

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
	memset(repose_old, 0, RESPONSELEN);
	memset(repose_new, 0, RESPONSELEN);
	memset(passphrase_old, 0, PASSPHRASELEN + 1);
	memset(passphrase_new, 0, PASSPHRASELEN + 1);

	return rc;
}

// vim: set syntax=c:
