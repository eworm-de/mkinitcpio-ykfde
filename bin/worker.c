/*
 * (C) 2014-2017 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * compile with:
 * $ gcc -o ykfde ykfde.c -liniparser -lkeyutils -lykpers-1 -lyubikey
 *
 * test with:
 * $ systemd-ask-password --no-tty "Please enter passphrase for disk foobar..."
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>

#include <iniparser.h>

#include <keyutils.h>

#include <yubikey.h>
#include <ykpers-1/ykdef.h>
#include <ykpers-1/ykcore.h>

#include "../config.h"

/* Yubikey supports write of 64 byte challenge to slot,
 * returns HMAC-SHA1 response.
 *
 * Lengths are defined in ykpers-1/ykdef.h:
 * SHA1_MAX_BLOCK_SIZE     64
 * SHA1_DIGEST_SIZE        20
 *
 * For passphrase we use hex encoded digest, that is
 * twice the length of binary digest. */
#define CHALLENGELEN	SHA1_MAX_BLOCK_SIZE
#define RESPONSELEN	SHA1_MAX_BLOCK_SIZE
#define PASSPHRASELEN	SHA1_DIGEST_SIZE * 2

#define ASK_PATH	"/run/systemd/ask-password/"
#define ASK_MESSAGE	"Please enter passphrase for disk"

/*** send_on_socket ***/
static int send_on_socket(int fd, const char *socket_name, const void *packet, size_t size) {
	union {
		struct sockaddr sa;
		struct sockaddr_un un;
	} sa = {
		.un.sun_family = AF_UNIX,
	};

	strncpy(sa.un.sun_path, socket_name, sizeof(sa.un.sun_path));

	if (sendto(fd, packet, size, MSG_NOSIGNAL, &sa.sa, offsetof(struct sockaddr_un, sun_path) + strlen(socket_name)) < 0) {
		perror("sendto() failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*** yk_open_and_check ***/
static YK_KEY * yk_open_and_check(const unsigned int expected, unsigned int * serial) {
	YK_KEY * yk;

	if ((yk = yk_open_first_key()) == NULL) {
		if (errno != EAGAIN)
			perror("yk_open_first_key() failed");
		goto out1;
	}

	if (serial != NULL) {
		/* read the serial number from key */
		if (yk_get_serial(yk, 0, 0, serial) == 0) {
			perror("yk_get_serial() failed");
			goto out2;
		}

		if (expected > 0 && expected != *serial) {
			fprintf(stderr, "Opened Yubikey with unexpected serial number (%d != %d)...\n", expected, *serial);
			goto out2;
		}
	}

	return yk;

out2:
	/* close Yubikey */
	if (yk_close_key(yk) == 0)
		perror("yk_close_key() failed");

out1:
	return NULL;
}

/*** read_challenge ***/
static int read_challenge(const unsigned int serial, char * challenge) {
	int rc = EXIT_FAILURE;
	char challengefilename[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 1];
	int challengefile;

	snprintf(challengefilename, sizeof(challengefilename), CHALLENGEDIR "/challenge-%d", serial);

	/* check if challenge file exists */
	if (access(challengefilename, R_OK) == -1) {
		goto out1;
	}

	/* read challenge from file */
	if ((challengefile = open(challengefilename, O_RDONLY)) < 0) {
		perror("Failed opening challenge file for reading");
		goto out1;
	}

	if (read(challengefile, challenge, CHALLENGELEN) < 0) {
		perror("Failed reading challenge from file");
		goto out2;
	}

	rc = EXIT_SUCCESS;

out2:
	close(challengefile);

out1:
	return rc;
}

/*** get_second_factor ***/
static char * get_second_factor(void) {
	key_serial_t key;
	void * payload = NULL;

	/* get second factor from key store
	 * If this fails it is not critical... possibly we just do not
	 * use second factor. */
	key = keyctl_search(KEY_SPEC_USER_KEYRING, "user", "ykfde-2f", 0);

	if (key > 0) {
		/* if we have a key id we have a key - so this should succeed */
		if (keyctl_read_alloc(key, &payload) < 0) {
			perror("Failed reading payload from key");
			return NULL;
		}

		return payload;
	}

	return NULL;
}

/*** get_response ***/
static int get_response(const unsigned int serial, uint8_t slot, char * challenge, char * passphrase) {
	YK_KEY * yk;
	char response[RESPONSELEN];
	char * second_factor;
	size_t second_factor_len;
	/* iniparser */
	dictionary * ini;
	char section_ykslot[10 /* unsigned int in char */ + 1 + sizeof(CONFYKSLOT) + 1];

	memset(response, 0, RESPONSELEN);

	if ((second_factor = get_second_factor()) != NULL) {
		/* we replace part of the challenge with the second factor */
		second_factor_len = strlen(second_factor);
		memcpy(challenge, second_factor, second_factor_len < CHALLENGELEN / 2 ?
				second_factor_len : CHALLENGELEN / 2);
		memset(second_factor, 0, second_factor_len);
		free(second_factor);
	}

	/* try to read config file
	 * If anything here fails we do not care... slot 2 is the default. */
	if ((ini = iniparser_load(CONFIGFILE)) != NULL) {
		/* first try the general setting */
		slot = iniparser_getint(ini, "general:" CONFYKSLOT, slot);

		sprintf(section_ykslot, "%d:" CONFYKSLOT, serial);

		/* then probe for setting with serial number */
		slot = iniparser_getint(ini, section_ykslot, slot);

		switch (slot) {
			case 1:
			case SLOT_CHAL_HMAC1:
				slot = SLOT_CHAL_HMAC1;
				break;
			case 2:
			case SLOT_CHAL_HMAC2:
			default:
				slot = SLOT_CHAL_HMAC2;
				break;
		}

		iniparser_freedict(ini);
	}

	/* open Yubikey and check serial */
	if ((yk = yk_open_and_check(serial, NULL)) == NULL) {
		fprintf(stderr, "yk_open_and_check() failed\n");
		goto out1;
	}

	/* do challenge/response and encode to hex */
	if (yk_challenge_response(yk, slot, true,
			CHALLENGELEN, (unsigned char *) challenge,
			RESPONSELEN, (unsigned char *) response) == 0) {
		perror("yk_challenge_response() failed");
		goto out2;
	}

	yubikey_hex_encode((char *) passphrase, (char *) response, SHA1_DIGEST_SIZE);

out2:
	/* close Yubikey */
	if (yk_close_key(yk) == 0)
		perror("yk_close_key() failed");

out1:
	memset(response, 0, RESPONSELEN);

	return EXIT_SUCCESS;
}

/*** add_keyring ***/
static int add_keyring(const char * passphrase) {
	key_serial_t key;

	/* add key to kernel key store
	 * Put it into session keyring first, set permissions and
	 * move it to user keyring. */
	if ((key = add_key("user", "cryptsetup", passphrase,
			PASSPHRASELEN, KEY_SPEC_USER_KEYRING)) < 0) {
		perror("add_key() failed");
		return -1;
	}

	if (keyctl_set_timeout(key, 150) < 0) {
		perror("keyctl_set_timeout() failed");
		return -1;
	}

	return EXIT_SUCCESS;
}

/*** answer_askpass ***/
static int answer_askpass(const char * ask_file, const char * passphrase) {
	int rc = EXIT_FAILURE, fd_askpass;
	const char * ask_message, * ask_socket;
	/* iniparser */
	dictionary * ini;

	if ((ini = iniparser_load(ask_file)) == NULL) {
		perror("cannot parse file");
		goto out1;
	}

	ask_message = iniparser_getstring(ini, "Ask:Message", NULL);

	if (strncmp(ask_message, ASK_MESSAGE, strlen(ASK_MESSAGE)) != 0)
		goto out2;

	if ((ask_socket = iniparser_getstring(ini, "Ask:Socket", NULL)) == NULL) {
		perror("Could not get socket name");
		goto out2;
	}

	if ((fd_askpass = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0)) < 0) {
		perror("socket() failed");
		goto out2;
	}

	if (send_on_socket(fd_askpass, ask_socket, passphrase, PASSPHRASELEN + 1) < 0) {
		perror("send_on_socket() failed");
		goto out3;
	}

	rc = EXIT_SUCCESS;

out3:
	close(fd_askpass);

out2:
	iniparser_freedict(ini);

out1:
	return rc;
}

/*** walk_askpass ***/
static int walk_askpass(const char * passphrase) {
	int rc = EXIT_FAILURE;
	DIR * dir;
	struct dirent * ent;

	/* change to directory so we do not have to assemble complete/absolute path */
	if (chdir(ASK_PATH) != 0) {
		perror("chdir() failed");
		return rc;
	}

	/* Is the request already there? */
	if ((dir = opendir(ASK_PATH)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "ask.", 4) == 0) {
				if ((rc = answer_askpass(ent->d_name, passphrase)) == EXIT_SUCCESS)
					goto out;
			}
		}
	} else {
		perror ("opendir() failed");
		return EXIT_FAILURE;
	}

	rc = EXIT_SUCCESS;

out:
	closedir(dir);

	return rc;
}

/*** main ***/
int main(int argc, char **argv) {
	int8_t rc = EXIT_FAILURE;
	/* Yubikey */
	YK_KEY * yk;
	uint8_t slot = SLOT_CHAL_HMAC2;
	unsigned int serial = 0;
	/* challenge and passphrase */
	char challenge[CHALLENGELEN + 1];
	char passphrase[PASSPHRASELEN + 2];

#ifdef DEBUG
	/* reopening stderr to /dev/console may help debugging... */
	FILE * tmp = freopen("/dev/console", "w", stderr);
	(void) tmp;
#endif

	/* check that we are running from systemd */
	if (sd_notify(0, "READY=0\nSTATUS=Work in progress...") <= 0) {
		fprintf(stderr, "This is expected to run from a systemd service.\n");
		goto out10;
	}

	/* initialize static memory */
	memset(challenge, 0, CHALLENGELEN + 1);
	memset(passphrase, 0, PASSPHRASELEN + 2);

	*passphrase = '+';

	/* init and open first Yubikey */
	if (yk_init() == 0) {
		perror("yk_init() failed");
		goto out10;
	}

	/* open Yubikey and get serial */
	if ((yk = yk_open_and_check(0, &serial)) == NULL) {
		if (errno == EAGAIN)
			rc = EXIT_SUCCESS;
		goto out30;
	}

	/* close Yubikey */
	if (yk_close_key(yk) == 0) {
		perror("yk_close_key() failed");
		goto out30;
	}

	if ((rc = read_challenge(serial, challenge)) < 0)
		goto out30;

	if ((rc = get_response(serial, slot, challenge, passphrase + 1)) < 0)
		goto out30;

	if ((rc = add_keyring(passphrase + 1)) < 0)
		goto out30;

	if ((rc = walk_askpass(passphrase)) < 0)
		goto out30;

out30:
	/* release Yubikey */
	if (yk_release() == 0)
		perror("yk_release() failed");

out10:
	/* wipe challenge from memory */
	memset(challenge, 0, CHALLENGELEN + 1);
	memset(passphrase, 0, PASSPHRASELEN + 2);

	/* notify systemd that we are ready
	   This does not indicate whether or not we are successful, but prevents
	   systemd from reporting: Failed with result 'protocol'. */
	sd_notify(0, "READY=1\nSTATUS=All done.");

	return rc;
}

// vim: set syntax=c:
