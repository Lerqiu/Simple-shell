#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  // Get pid of proces that changed status
  while ((pid = waitpid(WAIT_ANY, &status, WNOHANG | WUNTRACED | WCONTINUED)) >
         0) {
    // Search for proc in jobs
    for (int jobIndex = 0; jobIndex < njobmax; jobIndex++) {
      job_t *job = jobs + jobIndex;
      if (job->pgid == 0)
        continue;

      // Look in procs array of single job
      for (int procIndex = 0; procIndex < job->nproc; procIndex++) {
        proc_t *proc = job->proc + procIndex;

        // Right proc found
        if (proc->pid == pid) {
          // State change
          if (WIFSTOPPED(status)) {
            proc->state = STOPPED;
          } else if (WIFCONTINUED(status))
            proc->state = RUNNING;
          else {
            proc->state = FINISHED;
            proc->exitcode = status;
          }
          // Check if all proces in job have the same state
          bool same = true;
          for (int pInd = 0; pInd < job->nproc; pInd++) {
            proc_t *p = job->proc + pInd;
            if (p->state != proc->state)
              same = false;
          }
          // If yes update job state
          if (same) {
            job->state = proc->state;
          }

          // End search
          jobIndex = njobmax; // End outer loop
          break;              // Present loop
        }
      }
    }
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  sigset_t mask;

  // SIGCHLD blocking. We can't change structure parallel in sigchld handle when
  // deleting structure
  if (sigprocmask(SIG_BLOCK, &sigchld_mask, &mask) < 0)
    exit(EXIT_FAILURE);

  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }

  // SIGCHLD unblocking
  if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0)
    exit(EXIT_FAILURE);
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT
  if (bg == FG) { // FG
    movejob(j, 0);

    printf("[%d] %s '%s'\n", j, "continue", jobcmd(0));

    tcsetattr(tty_fd, TCSADRAIN, &(jobs[0].tmodes)); // Terminal configuration
    setfgpgrp(jobs[0].pgid);
    kill(-jobs[0].pgid, SIGCONT);

    jobs[0].state = RUNNING; // Mark new state in jobs struct

    monitorjob(mask); // Wait for child stop of finish state
  } else {            // BG
    kill(jobs[j].pgid, SIGCONT);
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  if (jobs[j].pgid != 0) {
    kill(-jobs[j].pgid, SIGTERM);
    kill(-jobs[j].pgid, SIGCONT);
  }
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    if (which == ALL || jobs[j].state == which) {
      // Still alive
      if (jobs[j].state == RUNNING || jobs[j].state == STOPPED) {
        char *state[] = {"running", "suspended"};
        printf("[%d] %s '%s'\n", j, state[jobs[j].state - 1], jobcmd(j));

        // Dead
      } else {
        int eCode = exitcode(&(jobs[j]));
        if (WIFSIGNALED(eCode))
          printf("[%d] %s '%s' by signal %d\n", j, "killed", jobcmd(j),
                 WTERMSIG(eCode));
        else
          printf("[%d] %s '%s', status=%d\n", j, "exited", jobcmd(j),
                 WEXITSTATUS(eCode));

        deljob(&(jobs[j]));
      }
    }
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT
  pid_t pid;
  job_t *fgJob = &(jobs[0]);

  // Waiting for proces in fg job
  while (fgJob->state == RUNNING &&
         (pid = waitpid(-fgJob->pgid, &state, WUNTRACED | WCONTINUED)) > 0) {
    // Look in procs array of single job
    for (int procIndex = 0; procIndex < fgJob->nproc; procIndex++) {
      proc_t *proc = fgJob->proc + procIndex;

      // Right proc found
      if (proc->pid == pid) {
        // Change state if need
        if (WIFSTOPPED(state)) {
          proc->state = STOPPED;
        } else if (WIFCONTINUED(state))
          proc->state = RUNNING;
        else {
          proc->state = FINISHED;
          proc->exitcode = WEXITSTATUS(state);
        }

        // Check if all proces in job have the same state
        bool same = true;
        for (int pInd = 0; pInd < fgJob->nproc; pInd++) {
          proc_t *p = fgJob->proc + pInd;
          if (p->state != proc->state)
            same = false;
        }
        // If yes update job state
        if (same) {
          fgJob->state = proc->state;
        }
        break;
      }
    }
  }

  setfgpgrp(getpgrp());                        // Return terminal back to shell
  tcgetattr(tty_fd, &(jobs[0].tmodes));        // Save user terminal settings
  tcsetattr(tty_fd, TCSADRAIN, &shell_tmodes); // Set shell terminal settings

  int newJ = 0;
  if (jobstate(newJ, &exitcode) == STOPPED) {
    newJ = allocjob();
    movejob(0, newJ);
  }
#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  // Iteration of all jobs
  for (int jobIndex = 0; jobIndex < njobmax; jobIndex++) {
    job_t *job = jobs + jobIndex;
    if (job->pgid == 0)
      continue;

    killjob(jobIndex); // Sending kill signal for each process in job
    pid_t pid;
    int state;

    // Clean over all children in jobs
    while (job->state != FINISHED &&
           (pid = waitpid(-job->pgid, &state, 0)) > 0) {

      // Look in procs array of single job
      for (int procIndex = 0; procIndex < job->nproc; procIndex++) {
        proc_t *proc = job->proc + procIndex;

        // Right proc found
        if (proc->pid == pid) {
          proc->state = FINISHED;
          proc->exitcode = state;
        }

        // Check if all proces in job have the same state
        bool same = true;
        for (int pInd = 0; pInd < job->nproc; pInd++) {
          proc_t *p = job->proc + pInd;
          if (p->state != proc->state)
            same = false;
        }
        // If yes update job state
        if (same) {
          job->state = proc->state;
        }
      }
    }
  }
#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
