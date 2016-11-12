#include <stdio.h>
#include <signal.h>
#include <android/log.h>

#define STAT_SIGNAL	SIGUSR2

extern void print_leaks(void);

static void sighandler(int signo, siginfo_t *siginfo, void *context)
{
  //  __android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder", "stat signal");
  print_leaks();
}

static __attribute__((constructor))
void stats_init(void)
{
	//	__android_log_print(ANDROID_LOG_DEBUG, "MemoryLeakFinder", "print_stats_init");
  struct sigaction act;
  act.sa_sigaction = sighandler;
  act.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&act.sa_mask);
  sigaction(STAT_SIGNAL, &act, NULL);
} 
