#include <time.h>

/* Structure defs for bbgen */

#define COL_GREEN	0
#define COL_CLEAR 	1
#define COL_BLUE  	2
#define COL_PURPLE 	3
#define COL_YELLOW	4
#define COL_RED		5

/* Info-link definitions. */
typedef struct {
	char	name[64];
	char	filename[64];
	char	urlprefix[20];	/* "/help", "/notes" etc. */
	void	*next;
} link_t;

/* Column definitions.                     */
/* Basically a list of all possible column */
/* names with links to their help-texts    */
typedef struct {
	char	name[20];
	link_t	*link;
	void	*next;
} col_t;

/* Measurement entry definition               */
/* This points to a column definition, and    */
/* contains the actual color of a measurement */
/* Linked list.                               */
typedef struct {
	col_t	*column;
	int	color;
	time_t	age;
	void	*next;
} entry_t;

/* Aux. list definition for loading state of all hosts into one list */
typedef struct {
	char	*hostname;
	entry_t	*entry;
	void	*next;
} state_t;

typedef struct {
	char	hostname[60];
	char	ip[16];
	int	dialup;
	link_t	*link;
	entry_t	*entries;
	int	color;		/* Calculated */
	void	*next;
} host_t;

/* Aux. list definition for quick access to host records */
typedef struct {
	host_t	*hostentry;
	void	*next;
} hostlist_t;

typedef struct {
	char	title[200];
	host_t	*hosts;
	void	*next;
} group_t;


typedef struct {
	char	name[20];
	char	title[200];
	int	color;		/* Calculated */
	void	*next;
	void	*subpages;
	group_t	*groups;
	host_t	*hosts;
} page_t;

