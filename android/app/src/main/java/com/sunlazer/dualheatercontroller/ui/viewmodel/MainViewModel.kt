package com.sunlazer.dualheatercontroller.ui.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.sunlazer.dualheatercontroller.model.ProgramRecipe
import com.sunlazer.dualheatercontroller.model.SystemStatus
import com.sunlazer.dualheatercontroller.network.ConnectionState
import com.sunlazer.dualheatercontroller.network.ControllerRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(
    private val repository: ControllerRepository = ControllerRepository()
) : ViewModel() {

    val connectionState: StateFlow<ConnectionState> = repository.connectionState
    val systemStatus: StateFlow<SystemStatus> = repository.systemStatus
    val recipes: StateFlow<List<ProgramRecipe>> = repository.recipes
    val logs: StateFlow<List<String>> = repository.logs
    val pingLatencyMs: StateFlow<Long> = repository.pingLatencyMs
    val errorMessage: StateFlow<String?> = repository.errorMessage

    private val _ipAddress = MutableStateFlow("192.168.4.1")
    val ipAddress: StateFlow<String> = _ipAddress.asStateFlow()

    private val _showRecipeDialog = MutableStateFlow(false)
    val showRecipeDialog: StateFlow<Boolean> = _showRecipeDialog.asStateFlow()

    private val _showServiceDialog = MutableStateFlow(false)
    val showServiceDialog: StateFlow<Boolean> = _showServiceDialog.asStateFlow()

    init {
        // Auto-connect to default SoftAP IP
        connect()
    }

    fun setIpAddress(ip: String) {
        _ipAddress.value = ip
    }

    fun connect() {
        repository.connect(_ipAddress.value, viewModelScope)
    }

    fun disconnect() {
        repository.disconnect()
    }

    fun sendButton(key: String) {
        viewModelScope.launch {
            repository.sendButton(key)
        }
    }

    fun startProcess() {
        viewModelScope.launch {
            repository.sendControlAction("start")
        }
    }

    fun forceStart() {
        viewModelScope.launch {
            repository.sendControlAction("force_start")
        }
    }

    fun cancelForceStart() {
        viewModelScope.launch {
            repository.sendControlAction("cancel_force")
        }
    }

    fun resetAlarm() {
        viewModelScope.launch {
            repository.sendControlAction("reset_alarm")
        }
    }

    fun emergencyStop() {
        viewModelScope.launch {
            repository.sendControlAction("stop")
        }
    }

    fun toggleStartMode() {
        viewModelScope.launch {
            repository.sendControlAction("toggle_start_mode")
        }
    }

    fun resetLimitFails() {
        viewModelScope.launch {
            repository.sendControlAction("reset_fails")
        }
    }

    fun jogUp() {
        viewModelScope.launch {
            repository.sendControlAction("jog_up")
        }
    }

    fun jogDown() {
        viewModelScope.launch {
            repository.sendControlAction("jog_down")
        }
    }

    fun toggleSsr1() {
        viewModelScope.launch {
            repository.sendControlAction("toggle_ssr1")
        }
    }

    fun toggleSsr2() {
        viewModelScope.launch {
            repository.sendControlAction("toggle_ssr2")
        }
    }

    fun selectProgram(idx: Int) {
        viewModelScope.launch {
            repository.sendControlAction("select_program", Pair("idx", idx.toString()))
        }
    }

    fun saveRecipe(recipe: ProgramRecipe) {
        viewModelScope.launch {
            repository.saveRecipe(recipe)
        }
    }

    fun syncRtcTime() {
        viewModelScope.launch {
            repository.syncPhoneTime()
        }
    }

    fun openRecipeDialog() {
        viewModelScope.launch {
            repository.refreshRecipes()
            _showRecipeDialog.value = true
        }
    }

    fun closeRecipeDialog() {
        _showRecipeDialog.value = false
    }

    fun openServiceDialog() {
        viewModelScope.launch {
            repository.refreshLogs()
            _showServiceDialog.value = true
        }
    }

    fun closeServiceDialog() {
        _showServiceDialog.value = false
    }
}
