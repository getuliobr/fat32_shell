/**
 *    Descrição: Programa que implementa um shell para uso da estrutura de dados da FAT32
 *    Autores: Getulio Coimbra Regis, Igor Lara de Oliveira
 *    Creation Date: 30 / 06 / 2022
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"

int main(int argc, char **argv) {
	if(argc != 2) {
		printf("Invalid parameter count: %d\n", argc);
		printf("Usage: %s fat32image.img\n", argv[0]);
		return 0;
	}

	const char *disk_name = argv[1];

	read_disk(disk_name);

	// Buffer de entrada do usuário
	char str[1024] = { 0 };
	// Buffer que guarda o comando do linux
	char cmd[1024];
	// Buffer que guarda os parâmetros quem vem após o comando
	char** args = (char**) malloc(sizeof(char*) * 1024);

	while(1) {
		// Input do Usuário
		printf("fatshell:[%s/] $ ", directory_stack_count ? directory_stack->name : "img");
		fgets(str, 1024, stdin);
		str[strcspn(str, "\n")] = '\0';
		
		// ----------------------- Formatação do input ---------------------------- //
		char* token = strtok(str, " ");
		int args_count = 0;

		while (token != NULL) {
			if(args_count == 0) strcpy(cmd, token);
			

			args[args_count] = (char*) malloc(sizeof(char) * strlen(token) + 1);
			strcpy(args[args_count++], token);

	
			args[args_count] = NULL;

			token = strtok(NULL, " ");
		}
		// ------------------------------------------------------------------------ //
		

		if(!strcmp(cmd, "exit")) {
			close_disk();
			break;
		};
		if(!strcmp(cmd, "cd")) {
			if(args_count != 2) printf("cd: Invalid parameter count\n");
			else cd(args[1]);
		};
		if(!strcmp(cmd, "info")) {
			info();	
		};
		if(!strcmp(cmd, "ls")) {
			ls();	
		};
		if(!strcmp(cmd, "cluster")) {
			if(args_count != 2) printf("cluster: Invalid parameter count\n");
			else {
				int cluster_number;
				sscanf(args[1], "%d", &cluster_number);
				cluster(cluster_number);
			}
		};
		if(!strcmp(cmd, "pwd")){
			pwd();
		};
		if(!strcmp(cmd, "attr")){
			if(args_count != 2) printf("attr: Invalid parameter count\n");
			else attr(args[1]);
		};
		if(!strcmp(cmd, "touch")) {
			if(args_count != 2) printf("touch: Invalid parameter count\n");
			else touch(args[1]);
		};
		if(!strcmp(cmd, "rm")) {
			if(args_count != 2) printf("rm: Invalid parameter count\n");
			else rm(args[1]);
		};
		if(!strcmp(cmd, "rmdir")) {
			if(args_count != 2) printf("rmdir: Invalid parameter count\n");
			else rmdir(args[1]);
		};
		if(!strcmp(cmd, "rename")) {
			if(args_count != 3) printf("rename: Invalid parameter count\n");
			else rename_dir_entry(args[1], args[2]);
		};
		if(!strcmp(cmd, "mkdir")) {
			if(args_count != 2) printf("mkdir: Invalid parameter count\n");
			else mkdir(args[1]);
		};
		// Libera a memória para uma próxima leitura do input
		for(int j = 0; j < args_count; j++) free(args[j]);
	}
	return 0;
}
