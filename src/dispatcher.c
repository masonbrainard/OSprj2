#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/wait.h>
#include <sys/stat.h>

#include "dispatcher.h"
#include "shell_builtins.h"
#include "parser.h"

int runcmd(struct command *pipeline, int prev_pipe[2], int stdout_cp[2]);

/**
 * dispatch_external_command() - run a pipeline of commands
 *
 * @pipeline:   A "struct command" pointer representing one or more
 *              commands chained together in a pipeline.  See the
 *              documentation in parser.h for the layout of this data
 *              structure.  It is also recommended that you use the
 *              "parseview" demo program included in this project to
 *              observe the layout of this structure for a variety of
 *              inputs.
 *
 * Note: this function should not return un;til all commands in the
 * pipeline have completed their execution.
 *
 * Return: The return status of the last command executed in the
 * pipeline.
 */
static int dispatch_external_command(struct command *pipeline)
{
	int std_cpy[2] = {dup(STDOUT_FILENO), dup(STDIN_FILENO)};
	int status = 0;

	status = runcmd(pipeline, std_cpy, std_cpy);

	//reassert stdout and stdin if they were changed
	dup2(std_cpy[0], STDIN_FILENO);
	dup2(std_cpy[1], STDOUT_FILENO);

	return status;
}

int runcmd(struct command *pipeline, int prev_pipe[2], int std_cp[2])
{
	int status = 0;
	int inputfile = 0;
	int outputfile = 0;

	//laying pipe
	int pipeRW[2];
	if (pipe(pipeRW) == -1) {
		fprintf(stderr, "Unable to lay pipe.\n");
		return -1;
	}

	//if input_file change
	if (pipeline->input_filename != NULL) {
		if ((inputfile = open(pipeline->input_filename, O_RDONLY, S_IRUSR)) == -1) {
			fprintf(stderr, "Error opening input file.\n");
			return 1;
		}
		dup2(inputfile, STDIN_FILENO);
	} else {
		dup2(prev_pipe[0], STDIN_FILENO);
	}

	//check output_type
	if (pipeline->output_type == COMMAND_OUTPUT_PIPE)
		dup2(pipeRW[1], STDOUT_FILENO);
	else if (pipeline->output_type == COMMAND_OUTPUT_FILE_APPEND) {
		int append = O_WRONLY | O_APPEND;
		if ((outputfile = open(pipeline->output_filename, append, S_IWUSR)) == -1) {
			fprintf(stderr, "Error opening output file to append.\n");
			return 1;
		}
		/*
		if(dup2(outputfile, STDOUT_FILENO) == -1)
		{
			fprintf(stderr, "dup2 failed from Append.\n");
			return 1;
		}*/
		dup2(outputfile, STDOUT_FILENO);
	}
	else if(pipeline->output_type == COMMAND_OUTPUT_FILE_TRUNCATE)
	{
		int truncate = O_WRONLY | O_TRUNC | O_CREAT;
		if ((outputfile = open(pipeline->output_filename, truncate, S_IWUSR)) == -1) {
			fprintf(stderr, "Error opening output file to create/truncate.\n");
			return 1;
		}
		if(dup2(outputfile, STDOUT_FILENO) == -1)
		{
			fprintf(stderr, "dup2 failed from Truncate.\n");
			return 1;
		}
	}
	else {
		dup2(std_cp[1], STDOUT_FILENO);
	}

	//fork
	pid_t pid;
	if ((pid = fork()) == -1) {
		fprintf(stderr, "Something is 'forking' wrong...\n");
		return 1;
	} 
	else if (pid == 0) //child
	{
		close(pipeRW[0]); //close read end of pipe
		status = execvp(pipeline->argv[0], pipeline->argv);
		fprintf(stderr, "%s: Command not found\n", pipeline->argv[0]);
		return 1;
	} else //parent
	{
		close(pipeRW[1]); //close write end of pipe
		waitpid(pid, &status, 0);
		//close input and output files
		if (inputfile != 0) {
			dup2(pipeRW[0], STDIN_FILENO);
			if(close(inputfile) == -1){
				fprintf(stderr, "Error closing input file.\n");
				return 1;
			}
		}
		if (outputfile != 0) {
			dup2(pipeRW[1], STDOUT_FILENO);
			if(close(outputfile) == -1){
				fprintf(stderr, "Error closing output file.\n");
				return 1;
			}
		}
	}

	if (pipeline->pipe_to != NULL) {
		status = runcmd(pipeline->pipe_to, pipeRW, std_cp);
	}
	
	return status;
}

/**
 * dispatch_parsed_command() - run a command after it has been parsed
 *
 * @cmd:                The parsed command.
 * @last_rv:            The return code of the previously executed
 *                      command.
 * @shell_should_exit:  Output parameter which is set to true when the
 *                      shell is intended to exit.
 *
 * Return: the return status of the command.
 */
static int dispatch_parsed_command(struct command *cmd, int last_rv,
				   bool *shell_should_exit)
{
	/* First, try to see if it's a builtin. */
	for (size_t i = 0; builtin_commands[i].name; i++) {
		if (!strcmp(builtin_commands[i].name, cmd->argv[0])) {
			/* We found a match!  Run it. */
			return builtin_commands[i].handler(
				(const char *const *)cmd->argv, last_rv,
				shell_should_exit);
		}
	}

	/* Otherwise, it's an external command. */
	return dispatch_external_command(cmd);
}

int shell_command_dispatcher(const char *input, int last_rv,
			     bool *shell_should_exit)
{
	int rv;
	struct command *parse_result;
	enum parse_error parse_error = parse_input(input, &parse_result);

	if (parse_error) {
		fprintf(stderr, "Input parse error: %s\n",
			parse_error_str[parse_error]);
		return -1;
	}

	/* Empty line */
	if (!parse_result)
		return last_rv;

	rv = dispatch_parsed_command(parse_result, last_rv, shell_should_exit);
	free_parse_result(parse_result);
	return rv;
}
