@{
    Master = @{
        Host = '10.18.18.101'
        InstallRoot = 'D:\98_Enzo\TaskFlowMaster'
        OutputRoot = 'D:\98_Enzo\TaskFlowMaster\runtime\master'
        HttpListenAddress = '0.0.0.0'
        HttpPort = 50080
        HttpRoute = '/task-flow/images'
    }

    Worker = @{
        InstallRoot = 'D:\98_Enzo\TaskFlowWorker'
        OutputRoot = 'D:\98_Enzo\TaskFlowWorker\runtime\worker'
        LocalWorkerCount = 1
        WorkerTaskThreads = 64
        AlgorithmDelayMs = 10
        Algorithm = @{
            Enabled = $true
            PluginName = 'tgv_etching'
            PluginPath = ''
            CreateFunc = 'CreatePluginEtching'
            DestroyFunc = 'DestroyPluginEtching'
            PersistInputImages = $false
            InputDir = ''
        }
        Nodes = @(
            @{
                Host = '10.18.18.103'
                Id = 'node-01'
            }
            @{
                Host = '10.18.18.104'
                Id = 'node-02'
            }
            @{
                Host = '10.18.18.105'
                Id = 'node-03'
            }
            @{
                Host = '10.18.18.106'
                Id = 'node-04'
            }
            @{
                Host = '10.18.18.107'
                Id = 'node-05'
            }
        )
    }

    RabbitMq = @{
        AmqpPort = 5672
        ManagementHost = '127.0.0.1'
        ManagementPort = 15672
        ApiPath = '/api'
        AdminUser = 'guest'
        AdminPass = 'guest'
        VHost = 'mc_integration'
        MasterUser = 'mc_master'
        MasterPass = 'master_secret'
        WorkerUser = 'mc_worker'
        WorkerPass = 'worker_secret'
    }

    Http = @{
        ServerThreadCount = 64
        ChunkBytes = 8388608
        ReadBufferBytes = 262144
        WriteBufferBytes = 262144
        SocketReceiveBufferBytes = 0
        SocketSendBufferBytes = 0
        SweepEnabled = $true
        SweepProfiles = @(
            @{
                Enabled = $false
                Name = 'baseline-16k-os'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 16384
                WriteBufferBytes = 16384
                SocketReceiveBufferBytes = 0
                SocketSendBufferBytes = 0
            }
            @{
                Enabled = $true
                Name = 'read-256k-os'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 262144
                WriteBufferBytes = 262144
                SocketReceiveBufferBytes = 0
                SocketSendBufferBytes = 0
            }
            @{
                Enabled = $false
                Name = 'read-1m-os'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 1048576
                WriteBufferBytes = 1048576
                SocketReceiveBufferBytes = 0
                SocketSendBufferBytes = 0
            }
            @{
                Enabled = $false
                Name = 'read-1m-sock-8m'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 1048576
                WriteBufferBytes = 1048576
                SocketReceiveBufferBytes = 8388608
                SocketSendBufferBytes = 8388608
            }
            @{
                Enabled = $false
                Name = 'read-4m-sock-8m'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 4194304
                WriteBufferBytes = 4194304
                SocketReceiveBufferBytes = 8388608
                SocketSendBufferBytes = 8388608
            }
            @{
                Enabled = $false
                Name = 'read-8m-sock-16m'
                ServerThreadCount = 64
                ChunkBytes = 8388608
                ReadBufferBytes = 8388608
                WriteBufferBytes = 8388608
                SocketReceiveBufferBytes = 16777216
                SocketSendBufferBytes = 16777216
            }
            @{
                Enabled = $false
                Name = 'whole-frame-read-1m'
                ServerThreadCount = 64
                ChunkBytes = 20971520
                ReadBufferBytes = 1048576
                WriteBufferBytes = 1048576
                SocketReceiveBufferBytes = 8388608
                SocketSendBufferBytes = 8388608
            }
            @{
                Enabled = $false
                Name = 'read-1m-server128'
                ServerThreadCount = 128
                ChunkBytes = 8388608
                ReadBufferBytes = 1048576
                WriteBufferBytes = 1048576
                SocketReceiveBufferBytes = 8388608
                SocketSendBufferBytes = 8388608
            }
        )
    }

    Test = @{
        ExpectedWorkerCount = 5
        DurationSeconds = 2
        PublishRatePerSecond = 50
        ImageSizeBytes = 20971520
        ImageStoreCapacityBytes = 4294967296
        MasterWritePublishThreads = 'Auto'
        MasterResultThreads = 'Auto'
        WorkerReadyTimeoutSeconds = 300
        TimeoutMs = 0
    }
}
