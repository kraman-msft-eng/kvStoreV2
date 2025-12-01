# Scripts

This directory contains all scripts for building, deploying, and running the KV Store Service.

## Directory Structure

```
scripts/
├── init/           # Initialization and build scripts
├── deploy/         # Deployment scripts for Azure VMs
├── run/            # Scripts to run the service locally or remotely
└── cleanup_main.ps1  # Cleanup utility
```

## Init Scripts

| Script | Platform | Description |
|--------|----------|-------------|
| `init/init_repo.ps1` | Windows | Initialize repository and install dependencies |
| `init/init_repo.sh` | Linux | Initialize repository on Linux |
| `init/build_linux.sh` | Linux | Build the Linux client and playground |

## Deploy Scripts

| Script | Description |
|--------|-------------|
| `deploy/deploy_all.ps1` | Deploy both server and client |
| `deploy/deploy_server.ps1` | Deploy KVStoreServer to Windows VM |
| `deploy/deploy_client.ps1` | Deploy KVClient/Playground to Linux VM |
| `deploy/deploy-linux-client.ps1` | Detailed Linux client deployment |

## Run Scripts

| Script | Description |
|--------|-------------|
| `run/run_azure_linux.ps1` | Run playground on Azure Linux VM (single server) |
| `run/run_azure_linux_node0.ps1` | Run playground targeting NUMA node 0 (port 8085) |
| `run/run_azure_linux_node1.ps1` | Run playground targeting NUMA node 1 (port 8086) |
| `run/run_azure_linux_both.ps1` | Run playground on both NUMA nodes simultaneously |
| `run/runLocal.ps1` | Run server locally on Windows |
| `run/run_local_wsl.sh` | Run client in WSL |

## Usage Examples

### Initialize on Windows
```powershell
cd scripts/init
.\init_repo.ps1
```

### Deploy to Azure
```powershell
cd scripts/deploy
.\deploy_all.ps1
```

### Run Tests on Azure
```powershell
cd scripts/run
# Single NUMA node
.\run_azure_linux.ps1

# Both NUMA nodes (for FX96 VMs)
.\run_azure_linux_both.ps1
```

## Configuration

Most scripts use environment variables or have configuration at the top of the file. Common settings:

- **Windows VM IP**: Server IP address
- **Linux VM IP**: Client IP address  
- **Ports**: 8085 (NUMA 0), 8086 (NUMA 1)
- **SSH credentials**: For remote execution
