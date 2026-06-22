#include "netdev.h"

static struct netdev *g_dev;

void netdev_register(struct netdev *dev) { g_dev = dev; }
struct netdev *netdev_get(void)          { return g_dev; }
