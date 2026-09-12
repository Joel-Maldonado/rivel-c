#ifndef RV_IDE_ANALYSIS_H
#define RV_IDE_ANALYSIS_H

/* Emit compiler diagnostics and semantic facts as JSON, without generating code.
 * A path of "-" reads an unsaved document from stdin. */
int ide_analyze(const char *path);

#endif
