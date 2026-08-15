package com.sunlazer.dualheatercontroller.network

import com.sunlazer.dualheatercontroller.model.ProgramRecipe
import com.sunlazer.dualheatercontroller.model.SystemStatus
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.text.SimpleDateFormat
import java.util.*

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
}

class ControllerRepository(private val apiService: ApiService = ApiService()) {

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _systemStatus = MutableStateFlow(SystemStatus())
    val systemStatus: StateFlow<SystemStatus> = _systemStatus.asStateFlow()

    private val _recipes = MutableStateFlow<List<ProgramRecipe>>(emptyList())
    val recipes: StateFlow<List<ProgramRecipe>> = _recipes.asStateFlow()

    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs: StateFlow<List<String>> = _logs.asStateFlow()

    private val _pingLatencyMs = MutableStateFlow<Long>(0L)
    val pingLatencyMs: StateFlow<Long> = _pingLatencyMs.asStateFlow()

    private val _errorMessage = MutableStateFlow<String?>(null)
    val errorMessage: StateFlow<String?> = _errorMessage.asStateFlow()

    private var pollingJob: Job? = null
    private var currentBaseUrl: String = "http://192.168.4.1"

    fun connect(ipAddress: String, scope: CoroutineScope) {
        val cleanIp = ipAddress.trim().removePrefix("http://").removePrefix("https://").removeSuffix("/")
        currentBaseUrl = "http://$cleanIp"
        _connectionState.value = ConnectionState.CONNECTING
        _errorMessage.value = null

        pollingJob?.cancel()
        pollingJob = scope.launch(Dispatchers.IO) {
            // Initial recipe fetch
            refreshRecipes()

            while (isActive) {
                val startTime = System.currentTimeMillis()
                val result = apiService.getStatus(currentBaseUrl)
                val duration = System.currentTimeMillis() - startTime

                if (result.isSuccess) {
                    val status = result.getOrNull()
                    if (status != null) {
                        _systemStatus.value = status
                        _connectionState.value = ConnectionState.CONNECTED
                        _pingLatencyMs.value = duration
                        _errorMessage.value = null
                    }
                } else {
                    _connectionState.value = ConnectionState.ERROR
                    _errorMessage.value = result.exceptionOrNull()?.localizedMessage ?: "Connection lost"
                }

                // High-speed 100ms telemetry poll
                delay(100)
            }
        }
    }

    fun disconnect() {
        pollingJob?.cancel()
        pollingJob = null
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    suspend fun sendButton(key: String): Boolean {
        if (_connectionState.value != ConnectionState.CONNECTED) return false
        val res = apiService.sendButton(currentBaseUrl, key)
        return res.isSuccess
    }

    suspend fun refreshRecipes() {
        val res = apiService.getRecipes(currentBaseUrl)
        if (res.isSuccess) {
            _recipes.value = res.getOrNull() ?: emptyList()
        }
    }

    suspend fun saveRecipe(recipe: ProgramRecipe): Boolean {
        val res = apiService.saveRecipe(currentBaseUrl, recipe)
        if (res.isSuccess) {
            refreshRecipes()
            return true
        }
        return false
    }

    suspend fun sendControlAction(action: String, extraParam: Pair<String, String>? = null): Boolean {
        val res = apiService.sendControl(currentBaseUrl, action, extraParam)
        return res.isSuccess
    }

    suspend fun syncPhoneTime(): Boolean {
        val sdf = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US)
        val isoNow = sdf.format(Date())
        val res = apiService.setRtcTime(currentBaseUrl, isoNow)
        return res.isSuccess
    }

    suspend fun refreshLogs() {
        val res = apiService.getLogs(currentBaseUrl)
        if (res.isSuccess) {
            _logs.value = res.getOrNull() ?: emptyList()
        }
    }
}
