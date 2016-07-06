/*
 * (C) 2014-2016 by Christian Hesse <mail@eworm.de>
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
#include <signal.h>
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
#define PID_PATH	"/run/ykfde.pid"

void received_signal(int signal) {
	/* Do nothing, just interrupt the sleep. */
	return;
}

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

static YK_KEY * yk_open_and_check(const unsigned int expected, unsigned int * serial) {
	YK_KEY * yk;

	if ((yk = yk_open_first_key()) == NULL) {
		perror("yk_open_first_key() failed");
		goto error;
	}

	if (serial != NULL) {
		/* read the serial number from key */
		if (yk_get_serial(yk, 0, 0, serial) == 0) {
			perror("yk_get_serial() failed");
			goto error;
		}

		if (expected > 0 && expected != *serial) {
			fprintf(stderr, "Opened Yubikey with unexpected serial number (%d != %d)... ", expected, *serial);
			goto error;
		}
	}

	return yk;

error:
	/* release Yubikey */
	if (yk != NULL && yk_release() == 0)
		perror("yk_release() failed");

	return NULL;
}

static int try_answer(const unsigned int serial, uint8_t slot, const char * ask_file, char * challenge) {
	int8_t rc = EXIT_FAILURE;
	YK_KEY * yk;
	dictionary * ini;
	const char * ask_message, * ask_socket;
	int fd_askpass;
	char response[RESPONSELEN],
		passphrase[PASSPHRASELEN + 1],
		passphrase_askpass[PASSPHRASELEN + 2];
	/* keyutils */
	key_serial_t key;
	void * payload = NULL;
	size_t plen;

	memset(response, 0, RESPONSELEN);
	memset(passphrase, 0, PASSPHRASELEN + 1);
	memset(passphrase_askpass, 0, PASSPHRASELEN + 2);

	/* get second factor from key store
	 * if this fails it is not critical... possibly we just do not
	 * use second factor */
	key = request_key("user", "ykfde-2f", NULL, 0);

	if (key > 0) {
		/* if we have a key id we have a key - so this should succeed */
		if (keyctl_read_alloc(key, &payload) < 0) {
			perror("Failed reading payload from key");
			goto out1;
		}

		/* we replace part of the challenge with the second factor */
		plen = strlen(payload);
		memcpy(challenge, payload, plen < CHALLENGELEN / 2 ? plen : CHALLENGELEN / 2);

		free(payload);
	}

	/* open Yubikey and check serial */
	if ((yk = yk_open_and_check(serial, NULL)) == NULL) {
		fprintf(stderr, "yk_open_and_check() failed");
		goto out1;
	}

	/* do challenge/response and encode to hex */
	if (yk_challenge_response(yk, slot, true,
			CHALLENGELEN, (unsigned char *) challenge,
			RESPONSELEN, (unsigned char *) response) == 0) {
		perror("yk_challenge_response() failed");
		goto out1;
	}

	/* close Yubikey */
	if (yk_close_key(yk) == 0) {
		perror("yk_close_key() failed");
		goto out1;
	}

	yubikey_hex_encode((char *) passphrase, (char *) response, SHA1_DIGEST_SIZE);

	/* add key to kernel key store */
	if ((key = add_key("user", "cryptsetup", passphrase, PASSPHRASELEN, KEY_SPEC_USER_KEYRING)) > 0) {
		if (keyctl_set_timeout(key, 150) < 0)
			perror("keyctl_set_timeout() failed");
	} else
		perror("add_key() failed");

	/* key is placed, no ask file... quit here */
	if (ask_file == NULL) {
		rc = key > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		goto out1;
	}

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

	sprintf(passphrase_askpass, "+%s", passphrase);

	if ((fd_askpass = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0)) < 0) {
		perror("socket() failed");
		goto out2;
	}

	if (send_on_socket(fd_askpass, ask_socket, passphrase_askpass, PASSPHRASELEN + 1) < 0) {
		perror("send_on_socket() failed");
		goto out3;
	}

	rc = EXIT_SUCCESS;

out3:
	close(fd_askpass);

out2:
	iniparser_freedict(ini);

out1:
	/* wipe response (cleartext password!) from memory */
	memset(response, 0, RESPONSELEN);
	memset(passphrase, 0, PASSPHRASELEN + 1);
	memset(passphrase_askpass, 0, PASSPHRASELEN + 2);

	return rc;
}

int main(int argc, char **argv) {
	int8_t rc = EXIT_FAILURE;
	FILE *pidfile;
	/* Yubikey */
	YK_KEY * yk;
	uint8_t slot = SLOT_CHAL_HMAC2;
	unsigned int serial = 0;
	/* iniparser */
	dictionary * ini;
	char section_ykslot[10 /* unsigned int in char */ + 1 + sizeof(CONFYKSLOT) + 1];
	/* read challenge */
	char challenge[CHALLENGELEN + 1];
	char challengefilename[sizeof(CHALLENGEDIR) + 11 /* "/challenge-" */ + 10 /* unsigned int in char */ + 1];
	int challengefile;
	/* read dir */
	DIR * dir;
	struct dirent * ent;

#ifdef DEBUG
	/* reopening stderr to /dev/console may help debugging... */
	FILE * tmp = freopen("/dev/console", "w", stderr);
	(void) tmp;
#endif

	if ((pidfile = fopen(PID_PATH, "w")) != NULL) {
		if (fprintf(pidfile, "%d", getpid()) < 0) {
			perror("Failed writing pid");
			fclose(pidfile);
			goto out10;
		}
		fclose(pidfile);
	} else {
		rc = EXIT_FAILURE;
		perror("Failed opening pid file");
		goto out10;
	}

	/* connect to signal */
	signal(SIGUSR1, received_signal);

	/* initialize static memory */
	memset(challenge, 0, CHALLENGELEN + 1);

	/* init and open first Yubikey */
	if (yk_init() == 0) {
		perror("yk_init() failed");
		goto out10;
	}

	/* open Yubikey and get serial */
	if ((yk = yk_open_and_check(0, &serial)) == NULL) {
		fprintf(stderr, "yk_open_and_check() failed");
		goto out30;
	}

	/* close Yubikey */
	if (yk_close_key(yk) == 0) {
		perror("yk_close_key() failed");
		goto out30;
	}

	sprintf(challengefilename, CHALLENGEDIR "/challenge-%d", serial);

	/* check if challenge file exists */
	if (access(challengefilename, R_OK) == -1) {
		goto out30;
	}

	/* read challenge from file */
	if ((challengefile = open(challengefilename, O_RDONLY)) < 0) {
		perror("Failed opening challenge file for reading");
		goto out30;
	}

	if (read(challengefile, challenge, CHALLENGELEN) < 0) {
		perror("Failed reading challenge from file");
		goto out40;
	}

	/* try to read config file
	 * if anything here fails we do not care... slot 2 is the default */
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

	/* change to directory so we do not have to assemble complete/absolute path */
	if (chdir(ASK_PATH) != 0) {
		perror("chdir() failed");
		goto out40;
	}

	/* Is the request already there? */
	if ((dir = opendir(ASK_PATH)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "ask.", 4) == 0) {
				if ((rc = try_answer(serial, slot, ent->d_name, challenge)) == EXIT_SUCCESS)
					goto out50;
			}
		}
	} else {
		perror ("opendir() failed");
		goto out50;
	}

	/* Wait for 90 seconds.
	 * The user has this time to enter his second factor, resulting in
	 * SIGUSR1 being sent to us. */
	sleep(90);

	/* try again, but for key store this time */
	rc = try_answer(serial, slot, NULL, challenge);

out50:
	/* close dir */
	closedir(dir);

out40:
	/* close the challenge file */
	if (challengefile)
		close(challengefile);
	/* Unlink it if we were successful, we can not try again later! */
	if (rc == EXIT_SUCCESS)
		unlink(challengefilename);

out30:
	/* close Yubikey */
	if (yk != NULL && yk_close_key(yk) == 0)
		perror("yk_close_key() failed");

	/* release Yubikey */
	if (yk_release() == 0)
		perror("yk_release() failed");

out10:
	/* wipe challenge from memory */
	memset(challenge, 0, CHALLENGELEN + 1);

	unlink(PID_PATH);

	return rc;
}

// vim: set syntax=c:
