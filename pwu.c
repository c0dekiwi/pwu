#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>

enum action_type {
	ACTION_ERROR,
	ACTION_HELP,
	ACTION_LIST,
	ACTION_PRINT,
	ACTION_NEW,
	ACTION_DELETE,
	ACTION_MODIFY,
	ACTION_FILE,
	ACTION_IMPORT,
};

enum field_type {
	FIELD_SERVICE,
	FIELD_PASSWORD,
	FIELD_NAME,
	FIELD_PHONE,
	FIELD_EMAIL,
	FIELD_LEN,
};

void print_fieldtype(enum field_type type) {
	switch (type) {
		case FIELD_SERVICE : { printf("SERVICE" ); } break;
		case FIELD_PASSWORD: { printf("PASSWORD"); } break;
		case FIELD_NAME    : { printf("NAME"    ); } break;
		case FIELD_PHONE   : { printf("PHONE"   ); } break;
		case FIELD_EMAIL   : { printf("EMAIL"   ); } break;
		case FIELD_LEN     : { printf("LEN"     ); } break;
	}
}

int program_usage() {
	printf(
		"Password Management Utility tool\n"
		"\n"
		"This is a small cli tool for password management with local storage.\n"
		"\n"
		"Usage: pwu <cmd> <options>\n"
		"Help: pwu --help\n"
		"\n"
	);
	return 0;
}

bool flag_isset(const char *set, char flag) {
	if (!set || strlen(set) < 2) return false;
	if (set[0] != '-') return false;

	for (size_t i = 1; set[i]; i++) {
		if (set[i] == '-') return false;
		if (set[i] == flag) return true;
	}
	return false;
}

char *parse_field_value(char *arg) {
	if (!arg) return NULL;

	char *value = NULL;
	size_t i = 0;

	while (arg[i] && arg[i] != '=') i++;

	if (arg[i++] == '=') {
		if (arg[i] == '"') {
			size_t j = 0;
			value = calloc(strlen(&arg[++i]), sizeof(*value));
			if (!value) return NULL;
			while (arg[i] && arg[i] != '"') {
				if (arg[i] == '\\') i++;
				value[j++] = arg[i++];
			}
		}
		else {
			return strdup(&arg[i]);
		}
	}
	else {
		return calloc(1, sizeof(char));
	}

	return value;
}

int parse_arg(char *arg, enum action_type *at, int *id, bool *strict, bool *where, char **fields) {
	if (!arg) return 1;

	if (*at == ACTION_HELP) {
		// arg should be a manual entry
		if (fields[0]) return 5;
		fields[0] = strdup(arg);
	}
	else if (arg[0] == '-') {
		if ((arg[1] != '-' && strchr(arg, 'h')) || !strcmp(arg, "--help")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_HELP;
		}
		if ((arg[1] != '-' && strchr(arg, 'i')) || !strcmp(arg, "--import")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_IMPORT;
		}
		if ((arg[1] != '-' && strchr(arg, 'f')) || !strcmp(arg, "--file")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_FILE;
		}
		if ((arg[1] != '-' && strchr(arg, 'p')) || !strcmp(arg, "--print")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_PRINT;
		}
		if ((arg[1] != '-' && strchr(arg, 'm')) || !strcmp(arg, "--modify")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_MODIFY;
		}
		if ((arg[1] != '-' && strchr(arg, 'd')) || !strcmp(arg, "--delete")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_DELETE;
		}
		if ((arg[1] != '-' && strchr(arg, 'l')) || !strcmp(arg, "--list")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_LIST;
		}
		if ((arg[1] != '-' && strchr(arg, 'n')) || !strcmp(arg, "--new")) {
			if (*at != ACTION_ERROR) return 2;
			*at = ACTION_NEW;
		}
		if ((arg[1] != '-' && strchr(arg, 's')) || !strcmp(arg, "--strict")) {
			if (*strict == true) return 2;
			*strict = true;
		}
		if ((arg[1] != '-' && strchr(arg, 'w')) || !strcmp(arg, "--where")) {
			if (*where == true) return 2;
			*where = true;
		}
	}
	else {
		if (*at == ACTION_FILE || *at == ACTION_IMPORT) {
			// arg should be file path
			if (*where == true) return 5;
			if (fields[0]) return 5;
			*where = true;
			fields[0] = strdup(arg);
		}
		else {
			int i;
			for (i = 0; arg[i]; i++) if (!isdigit(arg[i])) break;
			if (!arg[i]) {
				// arg should be an ID
				*id = atoi(arg);
			}
			else {
				// arg should be <field>=<value>
				if (!strncmp(arg, "SERVICE", 7)) {
					if (fields[FIELD_SERVICE]) return 4;
					fields[FIELD_SERVICE] = parse_field_value(arg);
				}
				else if (!strncmp(arg, "PASSWORD", 8)) {
					if (fields[FIELD_PASSWORD]) return 4;
					fields[FIELD_PASSWORD] = parse_field_value(arg);
				}
				else if (!strncmp(arg, "NAME", 4)) {
					if (fields[FIELD_NAME]) return 4;
					fields[FIELD_NAME] = parse_field_value(arg);
				}
				else if (!strncmp(arg, "PHONE", 5)) {
					if (fields[FIELD_PHONE]) return 4;
					fields[FIELD_PHONE] = parse_field_value(arg);
				}
				else if (!strncmp(arg, "EMAIL", 5)) {
					if (fields[FIELD_EMAIL]) return 4;
					fields[FIELD_EMAIL] = parse_field_value(arg);
				}
				else {
					return 3;
				}
			}
		}
	}

	return 0;
}

char *load_savefile_location() {
	char *sfloc;

	sfloc = getenv("PWU_SAVEFILE");
	if (!sfloc) {
		fprintf(stderr, "[ERROR] load_savefile: Could not get ENV variable PWU_SAVEFILE.\n");
	}
	return sfloc;
}

char *readentirefile(const char *filepath) {
	FILE *f;
	int rd;
	char buf[4096+1];
	char *file;
	size_t fsize;

	struct stat statbuf;
	rd = stat(filepath, &statbuf);
	if (rd < 0) {
		if (errno != ENOENT) {
			perror("stat");
			return NULL;
		}
		return calloc(1, sizeof(char));
	}
	fsize = statbuf.st_size;

	file = calloc(fsize, sizeof(*file));
	if (!file) {
		fprintf(stderr, "[ERROR] readentirefile: Out of memory\n");
		return NULL;
	}

	f = fopen(filepath, "r");
	if (!f) {
		fprintf(stderr, "[ERROR] readentirefile: Error on fopen.\n");
		perror("fopen");
		free(file);
		return NULL;
	}

	fsize = 0;
	do {
		rd = fread(buf, sizeof(*buf), 10, f);
		if (rd < 0) {
			fprintf(stderr, "[ERROR] readentirefile: Error on read [%d].\n", rd);
			free(file);
			fclose(f);
			return NULL;
		} // handle errors
		if (rd) {
			memcpy(&file[fsize], buf, rd);
			fsize += rd;
			file[fsize] = 0;
		}
	} while (rd);

	fclose(f);
	return file;
}

struct entry {
	int id;
	char *fields[FIELD_LEN];
};

struct entries {
	size_t cap;
	size_t size;
	struct entry *e;
};

struct string_view {
	size_t len;
	char *str;
};

struct entries *resize_entries(struct entries *entries) {
	if (!entries) return NULL;

	void *ptr;
	size_t newcap = entries->cap ? entries->cap * 2 : 128;

	ptr = calloc(newcap, sizeof(*entries->e));
	if (!ptr) return NULL;
	memcpy(ptr, entries->e, entries->size * sizeof(*entries->e));
	free(entries->e);
	entries->e = ptr;
	entries->cap = newcap;

	return entries;
}

void free_entries(struct entries *entries) {
	size_t i;
	size_t j;

	if (!entries->e) return;
	for (i = 0; i < entries->size; i++) {
		for (j = 0; j < FIELD_LEN; j++) {
			free(entries->e[i].fields[j]);
		}
	}
	free(entries->e);
	entries->e = NULL;
	entries->cap = 0;
	entries->size = 0;
}

int load_entries(const char *file, struct entries *entries) {
	char *filecontent = NULL;
	struct string_view filesv = {0};
	size_t n = 0;
	int field = 0;

//	printf("File: %s\n", file ? file : "(null)");

	if (!file || !entries) {
		fprintf(stderr, "[ERROR] load_entries: Parameter is (null).\n");
		return -1;
	}

	filecontent = readentirefile(file);
	if (!filecontent) {
		fprintf(stderr, "[ERROR] load_entries: Could not read from [%s].\n", file);
		return -1;
	}

	filesv.str = filecontent;
	filesv.len = strlen(filecontent);

	if (filesv.len == 0) {
		printf("File [%s] is empty. Initialising savefile.\n", file);
		free(filecontent);
		if (!resize_entries(entries)) {
			fprintf(stderr, "[ERROR] load_entries: Out of memory.\n");
			return -1;
		}
		return 0;
	}

	if (filesv.len < 14 || strncmp("PWUtili magic\n", filesv.str, 14)) {
		free(filecontent);
		fprintf(stderr, "[ERROR] load_entries: [%s] is not a valid savefile.\n", file);
		return -2;
	}
	filesv.len -= 14;
	filesv.str += 14;

	while (filesv.len) {
		if (entries->size >= entries->cap) {
			if (!resize_entries(entries)) {
				fprintf(stderr, "[ERROR] load_entries: Could not allocate memory for entries.\n");
				free_entries(entries);
				free(filecontent);
				return -1;
			}
		}

		entries->e[entries->size].id = entries->size;
		for (n = 0; n < filesv.len && filesv.str[n] != '\n'; n++);
		entries->e[entries->size].fields[field] = strndup(filesv.str, n);
		filesv.len -= n + 1;
		filesv.str += n + 1;

		field++;
		if (field >= FIELD_LEN) {
			field = 0;
			entries->size++;
		}
	}

	free(filecontent);

	if (entries->size < entries->cap &&
	    memcmp(&entries->e[entries->size], &(struct entry){0}, sizeof(struct entry))) {
		free(entries->e);
		*entries = (struct entries){0};
		fprintf(stderr, "[ERROR] load_entries: [%s] is not a valid savefile.\n", file);
		return -3;
	}
	return 0;
}

int save_entries(const char *file, struct entries *entries) {
	FILE *fd;

	fd = fopen(file, "w");
	if (!fd) {
		fprintf(stderr, "[ERROR] save_entries: Failed to open [%s].\n", file ? file : "(null)");
		return -1;
	}

	fprintf(fd, "PWUtili magic\n");

	for (size_t i = 0; i < entries->size; i++) {
		if (entries->e[i].id == -1) continue;
		for (size_t j = 0; j < FIELD_LEN; j++) {
			fprintf(fd, "%s\n", entries->e[i].fields[j]);
		}
	}

	fclose(fd);

	return 0;
}

void cmd_help(const char *cmd) {
	if (!cmd) {
		printf(
			"Usage: pwu <cmd> <options>\n"
			"\n"
			"Basics:\n"
			"  pwu --new\n"
			"  pwu --list\n"
			"  pwu --delete <id>\n"
			"\n"
			"For more information about the utility and a complete list of all commands, use:\n"
			"  pwu --help --help\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		printf(
			"Command: --help\n"
			"Alias: -h\n"
			"Usage:\n"
			"  pwu --help <subject>\n"
			"\n"
			"Display information about a function of the program.\n"
			"\n"
			"List of help pages:\n"
			". field\n"
			". alias\n"
			". id\n"
			"\n"
			". Commands:\n"
			"..  -h, --help\n"
			"..  -l, --list\n"
			"..  -p, --print\n"
			"..  -n, --new\n"
			"..  -d, --delete\n"
			"..  -m, --modify\n"
			"..  -f, --file\n"
			"..  -i, --import\n"
			"\n"
			". Modifiers:\n"
			"..  -s, --strict\n"
			"..  -w, --where\n"
		);
	}
	else if (!strcmp(cmd, "-l") || !strcmp(cmd, "--list")) {
		printf(
			"Command: --list\n"
			"Alias: -l\n"
			"Usage:\n"
			"  pwu --list\n"
			"  pwu --list <id>\n"
			"  pwu --list --where <FIELD>=\"<value>\"\n"
			"  pwu --list --strict --where <FIELD>=\"<value\"\n"
			"\n"
			"Lists all entries ID and SERVICE field.\n"
			"\n"
			"With <id>.\n"
			"Lists all fields for the entry with ID <id>\n"
			"\n"
			"With --where.\n"
			"Lists all entries which <FIELD> loosely contain <value>.\n"
			"Displays the ID, SERVICE field, and <FIELD> field for each entry.\n"
			"\n"
			"With --strict; (requires --where).\n"
			"Behavior is similar to --where without --strict but with a strict search.\n"
		);
	}
	else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--print")) {
		printf(
			"Command: --print\n"
			"Alias: -p\n"
			"Usage:\n"
			"  pwu --print <id>\n"
			"  pwu --print <id> <FIELD>\n"
			"\n"
			"If <FIELD> is not set; <FIELD> is set to PASSWORD.\n"
			"Prints the value of the field <FIELD> from the entry with ID <id>.\n"
			"\n"
			"This is intended to be piped into the clipboard for easy copy-pasta.\n"
			"eg. pwu -p 0 | xclip -sel clip\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-n") || !strcmp(cmd, "--new")) {
		printf(
			"Command: --new\n"
			"Alias: -n\n"
			"Usage:\n"
			"  pwu --new\n"
			"  pwu --new <FIELD>=<value>...\n"
			"\n"
			"Create a new entry.\n"
			"User will the be prompted to fill each field.\n"
			"\n"
			"All fields listed in the command line arguments will "
			"automatically be filled with the associated <value>.\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-d") || !strcmp(cmd, "--delete")) {
		printf(
			"Command: --delete\n"
			"Alias: -d\n"
			"Usage:\n"
			"  pwu --delete <id>\n"
			"  pwu --delete --where <FIELD>=<value>\n"
			"\n"
			"With <id>\n"
			"Deletes the entry where ID matches <id>.\n"
			"\n"
			"With --where; (exclusive with <id>).\n"
			"Deletes all entries where <FIELD> matches <value>.\n"
			"The deleted fields are exactly the ones that would have been listed if "
			"`pwu --list --strict --where <FIELD>=<value>` had been performed instead.\n"
			"\n"
			"/!\\ Caution /!\\: This is a dangerous operation as it will not ask for confirmation.\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-m") || !strcmp(cmd, "--modify")) {
		printf(
			"Command: --modify\n"
			"Alias: -m\n"
			"Usage:\n"
			"  pwu --modify <id> <FIELD>... <FIELD>=<value>...\n"
			"\n"
			"Modifies all listed <FIELD> of the entry where ID matches <id>.\n"
			"User will be prompted for the new value of each listed <FIELD>.\n"
			"Any <FIELD> with an associated <value> will be set without prompting.\n"
			"\n"
			"/!\\ Caution /!\\: This is a dangerous operation as it will not ask for confirmation.\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-f") || !strcmp(cmd, "--file")) {
		printf(
			"Command: --file\n"
			"Alias: -f\n"
			"Usage:\n"
			"  pwu --file\n"
			"  pwu --file <path_to_new_savefile>\n"
			"\n"
			"Prints the current location of the savefile.\n"
			"\n"
			"With <path_to_new_savefile>:\n"
			"Changes the savefile location to <path_to_new_savefile>.\n"
			"If no file exists there; creates one.\n"
			"If the file exists and is not a valid savefile; "
			"User will be asked for confirmation before wiping all contents in the file\n"
			"\n"
			"Does not copy the previous savefile's contents to the new savefile.\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-i") || !strcmp(cmd, "--import")) {
		printf(
			"Command: --import\n"
			"Alias: -i\n"
			"Usage:\n"
			"  pwu --import <path_to_file>\n"
			"\n"
			"Import all entries from <path_to_file>.\n"
			"<path_to_file> is required to be a valid savefile.\n"
			"\n"
		);
	}
	else if (!strcmp(cmd, "-s") || !strcmp(cmd, "--strict")) {
		printf(
			"Page missing.\n"
		);
	}
	else if (!strcmp(cmd, "-w") || !strcmp(cmd, "--where")) {
		printf(
			"Page missing.\n"
		);
	}
	else if (!strcmp(cmd, "field")) {
		printf(
			"Page missing.\n"
		);
	}
	else if (!strcmp(cmd, "id")) {
		printf(
			"Page missing.\n"
		);
	}
	else if (!strcmp(cmd, "alias")) {
		printf(
			"Page missing.\n"
		);
	}
	else {
		printf(
			"Unrecognised subject.\n"
			"\n"
			"Type `pwu --help` for more information.\n"
			"\n"
		);
	}
}

void cmd_import(const char *file, struct entries *entries) {
	struct entries imported_entries = {0};
	int c;

	if (!file) {
		printf("Please provide a file.\n");
		return;
	}
	c = load_entries(file, &imported_entries);
	if (c) {
		fprintf(stderr, "[ERROR] cmd_import: Failed to load entries from [%d]\n", c);
		return;
	}
	if (entries->cap < entries->size + imported_entries.size) {
		void *ptr = calloc(entries->size + imported_entries.size + 1, sizeof(*entries->e));
		if (!ptr) {
			free_entries(&imported_entries);
			fprintf(stderr, "[ERROR] cmd_import: Failed to expand memory for importing entries.\n");
			return;
		}
		memcpy(ptr, entries->e, entries->size * sizeof(*entries->e));
		free(entries->e);
		entries->e = ptr;
		entries->cap = entries->size + imported_entries.size + 1;
	}

	for (size_t i = 0; i < imported_entries.size; i++) {
		entries->e[entries->size].id = entries->size;
		for (size_t j = 0; j < FIELD_LEN; j++) {
			entries->e[entries->size].fields[j] = strdup(imported_entries.e[i].fields[j]);
		}
		entries->size++;
	}

	free_entries(&imported_entries);
}

void cmd_file(char **old, const char *new_location) {
	if (new_location) {
		printf("Not implemented.\n");
	}
	else {
		printf("%s\n", *old);
	}
}

void print_entry(struct entry *e) {
	printf("[%d] SERVICE : %s\n", e->id, e->fields[FIELD_SERVICE] );
	printf("[%d] PASSWORD: %s\n", e->id, e->fields[FIELD_PASSWORD]);
	printf("[%d] NAME    : %s\n", e->id, e->fields[FIELD_NAME]    );
	printf("[%d] PHONE   : %s\n", e->id, e->fields[FIELD_PHONE]   );
	printf("[%d] EMAIL   : %s\n", e->id, e->fields[FIELD_EMAIL]   );
}

void cmd_print(struct entries *entries, int id, char **fields) {
	int i = 0;

	while (i < FIELD_LEN && !fields[i]) i++;
	if (i == FIELD_LEN) i = FIELD_PASSWORD;

	if (id == -1 || id >= (int)entries->size) {
		printf("Please provide a valid id.\n");
	}
	else {
		printf("%s\n", entries->e[id].fields[i]);
	}
}

bool confirm(void) {
	// TODO: Prompt for confirmation

	printf("Confirm? yes/no\n");
	return true;
}

char *prompt_for_field(int field_index) {
	char buf[300] = {0};
	int rd;

	printf("Please enter value for ");
	print_fieldtype(field_index);
	printf(": ");
	fflush(stdout);

	fflush(stdin);
	rd = read(0, buf, 299);
	if (rd < 0) {
		fprintf(stderr, "[ERROR] prompt_for_field: Error on read.\n");
		perror("read");
		return NULL;
	}
	if (buf[rd-1] == '\n') {
		buf[rd-1] = 0;
	}

	char *field = strdup(buf);
	if (!field) {
		fprintf(stderr, "[ERROR] prompt_for_field: Memory.\n");
	}
	return field;
}

char *generate_password() {
	char *passwd;
	char buf[10] = {0};
	int rd = 0;

	while (!rd) {
		printf("How many characters for the password? ");
		fflush(stdout);

		fflush(stdin);
		memset(buf, 0, 10);
		rd = read(0, buf, 9);
		if (rd < 0) {
			fprintf(stderr, "[ERROR] generate_password.\n");
			perror("read");
			return NULL;
		}
		if (buf[rd-1] == '\n') {
			buf[--rd] = 0;
		}

		for (int i = 0; i < rd; i++) {
			if (!isdigit(buf[i])) {
				rd = 0;
			}
		}
	}

	rd = atoi(buf);
	passwd = calloc(rd, sizeof(*passwd));
	if (!passwd) {
		fprintf(stderr, "[ERROR] generate_password: Memory.\n");
		return NULL;
	}
	srand(time(NULL));
	for (int i = 0; i < rd; i++) {
		passwd[i] = (rand() % (127 - 32)) + 32;
	}

	printf(">%s.\n", passwd);
	return passwd;
}

bool validate_field(enum field_type type, char **value) {
	if (!value || !*value) return false;

	switch (type) {
		case FIELD_SERVICE : {
			if (!strlen(*value)) return false;
		} break;
		case FIELD_PASSWORD: {
			if (!strlen(*value)) {
				free(*value);
				*value = generate_password();
				if (!*value) return false;
			}
		} break;
		case FIELD_NAME    : {
			return true;
		} break;
		case FIELD_PHONE   : {
			return true;
		} break;
		case FIELD_EMAIL   : {
			if (!strlen(*value)) return false;
		} break;
		case FIELD_LEN: break;
	}
	return true;
}

void cmd_modify(struct entries *entries, int id, char **fields) {
	char *newval = NULL;

	if (id == -1 || id >= (int)entries->size) {
		printf("Please provide a valid ID.\n");
		return;
	}

	for (int i = 0; i < FIELD_LEN; i++) {
		if (fields[i]) {
			if (!strlen(fields[i])) {
				newval = NULL;
				do {
					free(newval);
					newval = prompt_for_field(i);
					if (!newval) return;
				} while (!validate_field(i, &newval));
				free(fields[i]);
				fields[i] = newval;
			}
			else {
				printf("New value for ");
				print_fieldtype(i);
				printf(": %s", fields[i]);
			}

			free(entries->e[id].fields[i]);
			entries->e[id].fields[i] = strdup(fields[i]);
		}
	}
}

void cmd_new(struct entries *entries, char **fields) {
	if (entries->cap < entries->size + 1) {
		if (!resize_entries(entries)) {
			fprintf(stderr, "[ERROR] cmd_new: Could not allocate memory.\n");
			return;
		}
	}
	for (int i = 0; i < FIELD_LEN; i++) {
		if (fields[i]) {
			if (!strlen(fields[i])) {
				free(fields[i]);
				fields[i] = NULL;
			}
			else {
				printf("Value for ");
				print_fieldtype(i);
				printf(": %s\n", fields[i]);
				entries->e[entries->size].fields[i] = strdup(fields[i]);
			}
		}
		if (!fields[i]) {
			char *val = NULL;
			do {
				free(val);
				val = prompt_for_field(i);
				if (!val) return;
			} while (!validate_field(i, &val));
			entries->e[entries->size].fields[i] = val;
		}
	}
	entries->e[entries->size].id = entries->size;
	entries->size++;
}

void cmd_delete(struct entries *entries, int id, bool where, char **fields) {
	(void)fields;

	if (id != -1) {
		entries->e[id].id = -1;
	}
	else if (where) {
		printf("Not implemented.\n");
	}
	else {
		printf("Invalid arguments. See `pwu --help --delete`.\n");
	}
}

void cmd_list(struct entries *entries, int id, bool strict, bool where, char **fields) {
	(void)fields;
	size_t i = 0;

	if (strict || where) {
		printf("Not implemented.\n");
	}
	else {
		if (id >= (int)entries->size) {
			printf("This ID does not exist.\n");
			return;
		}

		printf("\n");
		if (id == -1) {
			if (!entries->size) {
				printf("No entries found.\n");
			}
			for (i = 0; i < entries->size; i++) {
				printf("[%d] SERVICE: %s\n", entries->e[i].id, entries->e[i].fields[FIELD_SERVICE]);
			}
		}
		else {
			print_entry(&entries->e[id]);
		}
		printf("\n");
	}
}

int main(int ac, char **av) {
	enum action_type at = ACTION_ERROR;
	bool strict = false;
	bool where = false;
	int id = -1;
	char *fields[FIELD_LEN] = {0};
	int c;
	char *savefile_location;
	struct entries entries = {0};

	if (ac <= 1) return program_usage();

	savefile_location = load_savefile_location();
	if (!savefile_location) return 1;

	c = load_entries(savefile_location, &entries);
	if (c) return 1;

	if (ac > 1) {
		for (c = 1; c < ac; c++) {
			if (parse_arg(av[c], &at, &id, &strict, &where, fields)) {
				for (c = 0; c < FIELD_LEN; c++) free(fields[c]);
				printf("Invalid argument.\n");
				return 1;
			}
		}

		switch (at) {
			case ACTION_ERROR : { fprintf(stderr, "Invalid parameters.\n");         } break;
			case ACTION_HELP  : { cmd_help   (fields[0]);                           } break;
			case ACTION_IMPORT: { cmd_import (fields[0], &entries);                 } break;
			case ACTION_FILE  : { cmd_file   (&savefile_location, fields[0]);       } break;
			case ACTION_PRINT : { cmd_print  (&entries, id, fields);                } break;
			case ACTION_MODIFY: { cmd_modify (&entries, id, fields);                } break;
			case ACTION_DELETE: { cmd_delete (&entries, id, where, fields);         } break;
			case ACTION_LIST  : { cmd_list   (&entries, id, strict, where, fields); } break;
			case ACTION_NEW   : { cmd_new    (&entries, fields);                    } break;
		}

//		printf("Save file: %s\n", savefile_location ? savefile_location : "NONE");
//		printf("Action: %d\n", at);
//		printf("Strict? %s\n", strict ? "true" : "false");
//		printf("Where? %s\n", where ? "true" : "false");
//		printf("ID? %d\n", id);
		for (c = 0; c < FIELD_LEN; c++) {
//			printf("%d: %s\n", c, fields[c] ? fields[c] : "(nul)");
			free(fields[c]);
		}
		save_entries(savefile_location, &entries);
	}

	free_entries(&entries);
	return 0;
}
