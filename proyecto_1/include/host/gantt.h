#ifndef HOST_GANTT_H
#define HOST_GANTT_H

#include <stdio.h>
#include "host/statistics.h"

/* Escribir SVG por nodo dentro del HTML; los tramos son [tick, tick + duración) */
bool gantt_write(FILE *file, const Event *events, size_t count,
                  const ReportStatistics *statistics);

#endif // HOST_GANTT_H
