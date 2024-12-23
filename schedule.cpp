#include "schedule.h"
#include "kernel.h"
#include "constants.h"
#include "exceptions.h"
#include "vm.h"
#include "x86.h"
#include "syscalls.h"
#include "screen.h"

// Base-2 log function
int log2(int x)
{
    int i = 0;
    while (x > 1)
    {
        x /= 2;
        i++;
    }
    return i;
}

void scheduler(int current_pid)
{
    // 1. Check the KERNEL_CONFIGURATION to know if the scheduler should be active. If not, return without doing anything. If runScheduler is active, do the remaining steps here.
    struct kernelConfiguration *kernel_configuration = (struct kernelConfiguration *)KERNEL_CONFIGURATION;

    if (kernel_configuration->runScheduler == 0)
    {
        return;
    }
    else
    {
        // 2. Using the Task->recentRuntime and Task->priority, devise and implement an algorithm that will consider how much recent runtime a process has had, along with its priority to know which process to schedule next to run. Your algorithm must eliminate the possibility of starvation.
        int lowest_score = 2147483647; // Max INT possible for lowest score
        int next_task_pid = 0;

        // Iterate through all processes
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            struct task *Task = (struct task *)(PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * i));

            // Skip invalid or non-ready processes
            if (Task->pid != 0 && (Task->state == PROC_RUNNING || Task->state == PROC_SLEEPING))
            {
                // Penalize tasks with high recent runtime (linear scaling with a high multiplier)
                // Higher priority tasks get boosted (quadratic scaling)
                int score = (Task->recentRuntime > 0 ? log2(Task->recentRuntime + 1) * 15 : 0) -
                            (Task->priority > 0 ? Task->priority * Task->priority * 5 : 0);

                // Boost for tasks with long sleep time (quadratic scaling)
                if (Task->sleepTime > 500)
                {
                    score -= (Task->sleepTime * Task->sleepTime) / 200;
                }

                // Detect and address starvation (hard boost for long wait times)
                if (Task->sleepTime > 2000)
                {
                    score -= 10000; // Large negative for immediate scheduling
                }

                // Update next task
                if (score < lowest_score)
                {
                    lowest_score = score;
                    next_task_pid = Task->pid;
                }
            }
        }

        if (next_task_pid != 0 && next_task_pid != current_pid)
        {
            // 3. Once you know which process to run next, call sysSwitch() for that process. Be sure to set the recentRuntime in the task you are switching to back to zero before switching.
            struct task *next_task = (struct task *)(PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (next_task_pid - 1)));

            next_task->recentRuntime = 0;
            sysSwitch(next_task_pid, current_pid);
        }
    }
}