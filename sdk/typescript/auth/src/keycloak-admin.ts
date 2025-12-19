/**
 * @file keycloak-admin.ts
 * @brief Keycloak admin operations for automatic token exchange permission setup
 * 
 * This module provides functions to automatically configure token exchange
 * permissions in Keycloak when registering new clients.
 */

export interface KeycloakAdminConfig {
  serverUrl: string;
  realm: string;
  clientId: string;
  clientSecret: string;
  username?: string;
  password?: string;
}

/**
 * Keycloak Admin client for managing permissions
 */
export class KeycloakAdminClient {
  private config: KeycloakAdminConfig;
  private accessToken?: string;
  private tokenExpiresAt?: number;
  
  // Policy name for token exchange (must match what's configured in Keycloak)
  private readonly TOKEN_EXCHANGE_POLICY_NAME = 'allow-mcp-client-token-exchange';
  
  constructor(config: KeycloakAdminConfig) {
    this.config = config;
  }
  
  /**
   * Helper to construct Keycloak URLs properly
   */
  private getBaseUrl(): string {
    // If serverUrl already includes /realms/, extract base URL
    if (this.config.serverUrl.includes('/realms/')) {
      const realmIndex = this.config.serverUrl.indexOf('/realms/');
      return this.config.serverUrl.substring(0, realmIndex);
    }
    return this.config.serverUrl;
  }
  
  /**
   * Helper to get the realm from serverUrl if it includes it
   */
  private getRealm(): string {
    if (this.config.serverUrl.includes('/realms/')) {
      const match = this.config.serverUrl.match(/\/realms\/([^/]+)/);
      if (match) {
        return match[1];
      }
    }
    return this.config.realm;
  }
  
  /**
   * Get admin access token using client credentials or username/password
   */
  private async getAdminToken(): Promise<string> {
    // Check if we have a valid cached token
    if (this.accessToken && this.tokenExpiresAt && this.tokenExpiresAt > Date.now()) {
      return this.accessToken;
    }
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const tokenUrl = `${baseUrl}/realms/${realm}/protocol/openid-connect/token`;
    
    // Build request body based on available credentials
    const params = new URLSearchParams();
    
    if (this.config.username && this.config.password) {
      // Use password grant
      params.append('grant_type', 'password');
      params.append('client_id', this.config.clientId);
      params.append('client_secret', this.config.clientSecret);
      params.append('username', this.config.username);
      params.append('password', this.config.password);
    } else {
      // Use client credentials grant
      params.append('grant_type', 'client_credentials');
      params.append('client_id', this.config.clientId);
      params.append('client_secret', this.config.clientSecret);
    }
    
    console.log('🔑 Getting Keycloak admin token...');
    
    const response = await fetch(tokenUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: params.toString(),
    });
    
    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to get admin token: ${response.status} ${error}`);
    }
    
    const data = await response.json() as any;
    
    // Cache the token
    this.accessToken = data.access_token;
    this.tokenExpiresAt = Date.now() + ((data.expires_in || 300) - 30) * 1000; // 30 second buffer
    
    console.log('✅ Admin token obtained, expires in', data.expires_in, 'seconds');
    
    return this.accessToken!;
  }
  
  /**
   * Find a client by client ID
   */
  async findClient(clientId: string): Promise<any> {
    const token = await this.getAdminToken();
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const url = `${baseUrl}/admin/realms/${realm}/clients?clientId=${encodeURIComponent(clientId)}`;
    
    const response = await fetch(url, {
      method: 'GET',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
    });
    
    if (!response.ok) {
      throw new Error(`Failed to find client: ${response.status}`);
    }
    
    const clients = await response.json() as any[];
    return clients.length > 0 ? clients[0] : null;
  }
  
  /**
   * Enable management permissions on a client
   */
  async enableManagementPermissions(clientUuid: string): Promise<any> {
    const token = await this.getAdminToken();
    
    console.log(`   🔧 Enabling management permissions for client UUID: ${clientUuid}`);
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const url = `${baseUrl}/admin/realms/${realm}/clients/${clientUuid}/management/permissions`;
    
    const response = await fetch(url, {
      method: 'PUT',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ enabled: true }),
    });
    
    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to enable management permissions: ${response.status} ${error}`);
    }
    
    const permissions = await response.json();
    console.log('   ✅ Management permissions enabled');
    
    return permissions;
  }
  
  /**
   * Get the realm-management client
   */
  async getRealmManagementClient(): Promise<any> {
    const client = await this.findClient('realm-management');
    if (!client) {
      throw new Error('realm-management client not found');
    }
    return client;
  }
  
  /**
   * Find a policy by name
   */
  async findPolicy(realmMgmtClientId: string, policyName: string): Promise<any> {
    const token = await this.getAdminToken();
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const url = `${baseUrl}/admin/realms/${realm}/clients/${realmMgmtClientId}/authz/resource-server/policy/client?name=${encodeURIComponent(policyName)}`;
    
    const response = await fetch(url, {
      method: 'GET',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
    });
    
    if (!response.ok) {
      if (response.status === 404) {
        return null;
      }
      throw new Error(`Failed to find policy: ${response.status}`);
    }
    
    const policies = await response.json() as any[];
    return policies.length > 0 ? policies[0] : null;
  }
  
  /**
   * Update a client policy to add a new client
   */
  async addClientToPolicy(realmMgmtClientId: string, policyId: string, clientUuid: string): Promise<void> {
    const token = await this.getAdminToken();
    
    // Get the current policy
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const getPolicyUrl = `${baseUrl}/admin/realms/${realm}/clients/${realmMgmtClientId}/authz/resource-server/policy/client/${policyId}`;
    
    const getResponse = await fetch(getPolicyUrl, {
      method: 'GET',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
    });
    
    if (!getResponse.ok) {
      throw new Error(`Failed to get policy: ${getResponse.status}`);
    }
    
    const policy = await getResponse.json() as any;
    
    // Add the new client to the policy
    const clients = policy.clients || [];
    if (!clients.includes(clientUuid)) {
      clients.push(clientUuid);
      policy.clients = clients;
      
      // Update the policy
      const updateResponse = await fetch(getPolicyUrl, {
        method: 'PUT',
        headers: {
          'Authorization': `Bearer ${token}`,
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(policy),
      });
      
      if (!updateResponse.ok) {
        const error = await updateResponse.text();
        throw new Error(`Failed to update policy: ${updateResponse.status} ${error}`);
      }
      
      console.log(`   ✅ Added client to policy ${this.TOKEN_EXCHANGE_POLICY_NAME}`);
    } else {
      console.log(`   ✅ Client already in policy ${this.TOKEN_EXCHANGE_POLICY_NAME}`);
    }
  }
  
  /**
   * Associate policy with token-exchange permission
   */
  async associatePolicyWithPermission(
    realmMgmtClientId: string, 
    permissionId: string, 
    policyName: string
  ): Promise<void> {
    const token = await this.getAdminToken();
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const url = `${baseUrl}/admin/realms/${realm}/clients/${realmMgmtClientId}/authz/resource-server/permission/scope/${permissionId}`;
    
    // Get current permission
    const getResponse = await fetch(url, {
      method: 'GET',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
    });
    
    if (!getResponse.ok) {
      throw new Error(`Failed to get permission: ${getResponse.status}`);
    }
    
    const permission = await getResponse.json() as any;
    
    // Add policy if not already there
    const policies = permission.policies || [];
    if (!policies.includes(policyName)) {
      policies.push(policyName);
      permission.policies = policies;
      
      // Update permission
      const updateResponse = await fetch(url, {
        method: 'PUT',
        headers: {
          'Authorization': `Bearer ${token}`,
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(permission),
      });
      
      if (!updateResponse.ok) {
        const error = await updateResponse.text();
        throw new Error(`Failed to update permission: ${updateResponse.status} ${error}`);
      }
      
      console.log('   ✅ Associated policy with token-exchange permission');
    } else {
      console.log('   ✅ Policy already associated with token-exchange permission');
    }
  }
  
  /**
   * Enable token exchange permission for a client
   * This is the main function that orchestrates all the steps
   */
  async enableTokenExchangePermission(clientId: string): Promise<void> {
    try {
      console.log(`🔧 Enabling token-exchange permission for client: ${clientId}`);
      
      // Step 1: Find the client
      const client = await this.findClient(clientId);
      if (!client) {
        throw new Error(`Client not found: ${clientId}`);
      }
      const clientUuid = client.id;
      console.log(`   ✅ Found client UUID: ${clientUuid}`);
      
      // Step 2: Enable management permissions
      const permissions = await this.enableManagementPermissions(clientUuid);
      const tokenExchangePermissionId = permissions.scopePermissions?.['token-exchange'];
      
      if (!tokenExchangePermissionId) {
        throw new Error('token-exchange permission not found after enabling permissions');
      }
      console.log(`   ✅ Found token-exchange permission ID: ${tokenExchangePermissionId}`);
      
      // Step 3: Find realm-management client
      const realmMgmtClient = await this.getRealmManagementClient();
      const realmMgmtClientId = realmMgmtClient.id;
      console.log(`   ✅ Found realm-management client UUID: ${realmMgmtClientId}`);
      
      // Step 4: Find or create the policy
      let policy = await this.findPolicy(realmMgmtClientId, this.TOKEN_EXCHANGE_POLICY_NAME);
      
      if (!policy) {
        // Create the policy if it doesn't exist
        console.log(`   ⚠️  Policy ${this.TOKEN_EXCHANGE_POLICY_NAME} not found, creating it...`);
        policy = await this.createClientPolicy(realmMgmtClientId, clientUuid);
      }
      
      // Step 5: Add client to the policy
      await this.addClientToPolicy(realmMgmtClientId, policy.id, clientUuid);
      
      // Step 6: Associate policy with token-exchange permission
      await this.associatePolicyWithPermission(
        realmMgmtClientId,
        tokenExchangePermissionId,
        this.TOKEN_EXCHANGE_POLICY_NAME
      );
      
      console.log(`✅ Token exchange permission setup complete for client: ${clientId}`);
      
    } catch (error: any) {
      console.error(`❌ Failed to enable token-exchange permission for client ${clientId}:`, error);
      throw error;
    }
  }
  
  /**
   * Create a new client policy for token exchange
   */
  private async createClientPolicy(realmMgmtClientId: string, clientUuid: string): Promise<any> {
    const token = await this.getAdminToken();
    
    const baseUrl = this.getBaseUrl();
    const realm = this.getRealm();
    const url = `${baseUrl}/admin/realms/${realm}/clients/${realmMgmtClientId}/authz/resource-server/policy/client`;
    
    const policy = {
      type: 'client',
      logic: 'POSITIVE',
      decisionStrategy: 'UNANIMOUS',
      name: this.TOKEN_EXCHANGE_POLICY_NAME,
      description: 'Allow MCP clients to perform token exchange',
      clients: [clientUuid]
    };
    
    const response = await fetch(url, {
      method: 'POST',
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(policy),
    });
    
    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to create policy: ${response.status} ${error}`);
    }
    
    const createdPolicy = await response.json();
    console.log(`   ✅ Created policy ${this.TOKEN_EXCHANGE_POLICY_NAME}`);
    
    return createdPolicy;
  }
  
  /**
   * Check if a client has token exchange permission enabled
   */
  async hasTokenExchangePermission(clientId: string): Promise<boolean> {
    try {
      const client = await this.findClient(clientId);
      if (!client) {
        return false;
      }
      
      const realmMgmtClient = await this.getRealmManagementClient();
      const policy = await this.findPolicy(realmMgmtClient.id, this.TOKEN_EXCHANGE_POLICY_NAME);
      
      if (!policy) {
        return false;
      }
      
      // Check if client is in the policy
      return policy.clients?.includes(client.id) || false;
      
    } catch (error) {
      console.error('Failed to check token exchange permission:', error);
      return false;
    }
  }
}

// Singleton instance
let adminClient: KeycloakAdminClient | null = null;

/**
 * Initialize the Keycloak admin client
 */
export function initializeKeycloakAdmin(config?: KeycloakAdminConfig): KeycloakAdminClient {
  if (!adminClient) {
    const adminConfig = config || {
      serverUrl: process.env.GOPHER_AUTH_SERVER_URL || process.env.KEYCLOAK_URL || '',
      realm: process.env.KEYCLOAK_REALM || 'gopher-mcp-auth',
      clientId: process.env.KEYCLOAK_ADMIN_CLIENT_ID || process.env.GOPHER_CLIENT_ID || '',
      clientSecret: process.env.KEYCLOAK_ADMIN_CLIENT_SECRET || process.env.GOPHER_CLIENT_SECRET || '',
      username: process.env.KEYCLOAK_ADMIN_USERNAME,
      password: process.env.KEYCLOAK_ADMIN_PASSWORD,
    };
    
    adminClient = new KeycloakAdminClient(adminConfig);
  }
  return adminClient;
}

/**
 * Get the Keycloak admin client instance
 */
export function getKeycloakAdmin(): KeycloakAdminClient {
  if (!adminClient) {
    return initializeKeycloakAdmin();
  }
  return adminClient;
}