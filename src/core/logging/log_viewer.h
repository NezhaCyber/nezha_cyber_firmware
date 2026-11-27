#ifndef __LOG_VIEWER_H__
#define __LOG_VIEWER_H__

/**
 * Real-time on-device viewer for /NezhaLogs/nezha.log.
 *
 * The viewer never keeps the log history in RAM: every refresh reads the tail
 * of the SD file into one small transient buffer, draws it, and releases it.
 */
void systemLogViewer();

/** Menu with view / level / clear actions. */
void systemLogMenu();

#endif
