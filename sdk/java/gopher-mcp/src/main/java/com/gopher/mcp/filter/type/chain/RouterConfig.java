package com.gopher.mcp.filter.type.chain;

/**
 * Configuration for creating a chain router.
 */
public class RouterConfig {

    public int strategy;
    public int hashSeed;
    public long routeTable;
    public Object customRouterData;

    public RouterConfig(int strategy) {
        this.strategy = strategy;
        this.hashSeed = 0;
        this.routeTable = 0;
        this.customRouterData = null;
    }

    public int getStrategy() {
        return strategy;
    }

    public void setStrategy(int strategy) {
        this.strategy = strategy;
    }

    public int getHashSeed() {
        return hashSeed;
    }

    public void setHashSeed(int hashSeed) {
        this.hashSeed = hashSeed;
    }

    public long getRouteTable() {
        return routeTable;
    }

    public void setRouteTable(long routeTable) {
        this.routeTable = routeTable;
    }

    public Object getCustomRouterData() {
        return customRouterData;
    }

    public void setCustomRouterData(Object customRouterData) {
        this.customRouterData = customRouterData;
    }
}
