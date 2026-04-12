/*
 * $Id: compio_obj.h,v 1.2 2020-11-12 20:29:38 phil Exp $
 */

struct io_obj *compio_open(struct io_obj *iop, int flags,
			   int format, int complevel, int dir);
