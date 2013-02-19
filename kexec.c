/*
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <limits.h>
#include <stdbool.h>

#define MAXLINE (1024)
#ifdef SLEEP
#define SLEEP_MS (100) /* sleep this time between multi-port knocks */
#endif
#define STREQ(a,b) (strcmp((a),(b)) == 0)

static char *find_config_entry(char * const);
static int knock(char *, char *);
static void usage(void);

static const char *command = "kexec";
static bool verbose = false;

int main(int argc, char **argv)
{

   command = argv[0];

   /* getopt considered harmful */
   if (argc < 2)
      usage(); /* does not return */
   if (argc >= 3 && STREQ(argv[1], "-v"))
      verbose = true;

   pid_t pid = fork();
   switch (pid) 
   {
      case -1:  /* error */
         perror("fork");
         exit(EXIT_FAILURE);
         break; /* never reached */
      case 0: /* child */
         {
            char *host = basename(argv[0]);
            char *config = find_config_entry(host); /* strduped */
            if (config == NULL)
            {
               fputs("No configuration entry found\n", stderr);
               exit(EXIT_FAILURE);
            }

            /* remove hostname and ":" from config */
            char *sequence = config + strlen(host) + 1;

            char *protoport = strtok(sequence, ",");
            if (protoport != NULL)
            {
               if (knock(host, protoport) == -1)
                  if (config != NULL) 
                  {
                     free(config);
                     exit(EXIT_FAILURE);
                  }

               while((protoport = strtok(NULL, ",")) != NULL)
               {
#ifdef SLEEP
                  usleep(SLEEP_MS * 1000);
#endif
                  if (knock(host, protoport) == -1)
                     if (config != NULL) 
                     {
                        free(config);
                        exit(EXIT_FAILURE);
                     }
               }
            }
            
            if (config != NULL) 
               free(config);

            /* give server 1 second to open the port */
            if (verbose)
               puts("sleeping...");
            sleep(1);

            verbose == true ? argv += 2 : argv++;

            if (verbose)
            {
               fputs("executing: ", stdout);
               for (int i = 0; argv[i] != NULL; ++i)
                  printf("%s ", argv[i]);
               putchar('\n');
            }

            if (execvp(argv[0], argv) == -1)
            {
               perror("exec");
               exit(EXIT_FAILURE);
            }
         }
         break; /* never reached */
      default: /* parent */
         {
            int status;
            pid_t tpid;
            do
            {
              tpid = wait(&status);
            } while (tpid != pid);
            exit(WEXITSTATUS(status));
         }
         break; /* never reached */
   }

   /* never reached */
   return EXIT_SUCCESS;
}

static char *find_config_entry(char * const host)
{
   char path[PATH_MAX] = {0};
   snprintf(path, PATH_MAX, "%s/.kexec", getenv("HOME"));

   FILE *fp = fopen(path, "r");
   if (fp == NULL)
   {
      perror("fdopen");
      exit(EXIT_FAILURE);
   }

   char *ret = NULL;

   char buf[MAXLINE];
   while (fgets(buf, MAXLINE, fp) != NULL)
   {
      /* remove spaces from line */
      for (size_t i = 0, j = 0; (buf[j] = buf[i]) != '\0'; j += !isspace(buf[i++]));

      if (buf[0] == '#') /* comment */
         continue;

      /* find host */
      if (strstr(buf, host) != NULL)
      {
         ret = strdup(buf);
         break;
      }
   }

   fclose(fp);

   return ret;
}

/* knocking magic partially taken from judd vinet:
 * github.com/jvinet/knock */
static int knock(char *host, char *protoport)
{
   char *portstr = protoport + 4; /* jump over "[tcp|udp]:" */
   char *endptr;
   long int val;
   
   errno = 0; /* To distinguish success/failure after call */
   val = strtol(portstr, &endptr, 10);

   /* Check for various possible errors */
   if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
         || (errno != 0 && val == 0))
   {
      perror("strtol");
      return -1;
   }

   if (endptr == portstr)
   {
      fputs("No digits were found\n", stderr);
      return -1;
   }

   if (val < 1 || val > UINT16_MAX)
   {
      fputs("not a valid port range\n", stderr);
      return -1;
   }

   uint16_t port = (uint16_t)val;

   struct hostent *hostent = gethostbyname(host);
   struct sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = *((uint32_t*)hostent->h_addr_list[0]);
   addr.sin_port = htons(port);

   if (protoport[0] == 'u') /* udp */
   {
      int sd = socket(PF_INET, SOCK_DGRAM, 0);
      if(sd == -1) 
      {
         perror("socket");
         return -1;
      }
      if (verbose)
         printf("hitting udp %s:%u\n", inet_ntoa(addr.sin_addr), port);

      sendto(sd, "", 1, 0, (struct sockaddr*)&addr, sizeof(addr));
   }
   else if (protoport[0] == 't') /* tcp */
   {
      int sd = socket(PF_INET, SOCK_STREAM, 0);
      int flags;
      if(sd == -1)
      {
         perror("socket");
         return -1;
      }
      flags = fcntl(sd, F_GETFL, 0);
      fcntl(sd, F_SETFL, flags | O_NONBLOCK);

      if (verbose)
         printf("hitting tcp %s:%u\n", inet_ntoa(addr.sin_addr), port);

      connect(sd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
   }
   else /* unknown */
   {
      fputs("unknown protocol\n", stderr);
      return -1;
   }

   return 0;
}

static void usage(void)
{
   fprintf(stderr, "%s (symlink to hostname) COMMAND [OPTIONS]\n", command);
   exit(EXIT_FAILURE);
}
