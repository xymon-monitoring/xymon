#include <time.h>

/* Structure defs for bbgen */

#define COL_GREEN	0
#define COL_CLEAR 	1
#define COL_BLUE  	2
#define COL_PURPLE 	3
#define COL_YELLOW	4
#define COL_RED		5

/* Column definitions.                     */
/* Basically a list of all possible column */
/* names with links to their help-texts    */
typedef struct {
	char	name[20];
	char	link[200];
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

typedef struct {
	char	hostname[60];
	char	link[200];
	entry_t	*entries;
	void	*next;
} host_t;

typedef struct {
	char	title[200];
	host_t	*hosts;
	void	*next;
} group_t;


typedef struct {
	char	name[20];
	char	title[200];
	int	color;
	void	*next;
	void	*subpages;
	group_t	*groups;
	host_t	*hosts;
} page_t;

