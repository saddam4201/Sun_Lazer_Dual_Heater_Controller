package com.sunlazer.dualheatercontroller.network

import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import com.sunlazer.dualheatercontroller.model.ProgramRecipe
import com.sunlazer.dualheatercontroller.model.SystemStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.FormBody
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import java.util.concurrent.TimeUnit

class ApiService {
    private val client = OkHttpClient.Builder()
        .connectTimeout(2, TimeUnit.SECONDS)
        .readTimeout(2, TimeUnit.SECONDS)
        .writeTimeout(2, TimeUnit.SECONDS)
        .build()

    private val gson = Gson()

    suspend fun getStatus(baseUrl: String): Result<SystemStatus> = withContext(Dispatchers.IO) {
        try {
            val url = "$baseUrl/api/status"
            val request = Request.Builder().url(url).get().build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string() ?: ""
                    val status = gson.fromJson(body, SystemStatus::class.java)
                    Result.success(status)
                } else {
                    Result.failure(Exception("HTTP ${response.code}: ${response.message}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun sendButton(baseUrl: String, key: String): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val url = "$baseUrl/api/button?key=$key"
            val emptyBody = FormBody.Builder().build()
            val request = Request.Builder().url(url).post(emptyBody).build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    Result.success(true)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun getRecipes(baseUrl: String): Result<List<ProgramRecipe>> = withContext(Dispatchers.IO) {
        try {
            val url = "$baseUrl/api/recipes"
            val request = Request.Builder().url(url).get().build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string() ?: ""
                    val type = object : TypeToken<List<ProgramRecipe>>() {}.type
                    val recipes = gson.fromJson<List<ProgramRecipe>>(body, type)
                    Result.success(recipes)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun saveRecipe(baseUrl: String, recipe: ProgramRecipe): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val httpUrl = "$baseUrl/api/recipe".toHttpUrlOrNull()?.newBuilder()
                ?.addQueryParameter("idx", recipe.idx.toString())
                ?.addQueryParameter("name", recipe.name)
                ?.addQueryParameter("h1_setpoint", recipe.h1Setpoint.toString())
                ?.addQueryParameter("h2_setpoint", recipe.h2Setpoint.toString())
                ?.addQueryParameter("process_time_sec", recipe.processTimeSec.toString())
                ?.addQueryParameter("torque_limit", recipe.torqueLimit.toString())
                ?.addQueryParameter("temp_tolerance", recipe.tempTolerance.toString())
                ?.addQueryParameter("h1_Kp", recipe.h1Kp.toString())
                ?.addQueryParameter("h1_Ki", recipe.h1Ki.toString())
                ?.addQueryParameter("h1_Kd", recipe.h1Kd.toString())
                ?.addQueryParameter("h2_Kp", recipe.h2Kp.toString())
                ?.addQueryParameter("h2_Ki", recipe.h2Ki.toString())
                ?.addQueryParameter("h2_Kd", recipe.h2Kd.toString())
                ?.build() ?: return@withContext Result.failure(Exception("Invalid URL"))

            val emptyBody = FormBody.Builder().build()
            val request = Request.Builder().url(httpUrl).post(emptyBody).build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    Result.success(true)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun sendControl(baseUrl: String, action: String, extraParam: Pair<String, String>? = null): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val urlBuilder = "$baseUrl/api/control".toHttpUrlOrNull()?.newBuilder()
                ?.addQueryParameter("action", action)
            if (extraParam != null) {
                urlBuilder?.addQueryParameter(extraParam.first, extraParam.second)
            }
            val httpUrl = urlBuilder?.build() ?: return@withContext Result.failure(Exception("Invalid URL"))
            val emptyBody = FormBody.Builder().build()
            val request = Request.Builder().url(httpUrl).post(emptyBody).build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    Result.success(true)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun setRtcTime(baseUrl: String, isoString: String): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val url = "$baseUrl/rtc/set?iso=$isoString"
            val emptyBody = FormBody.Builder().build()
            val request = Request.Builder().url(url).post(emptyBody).build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    Result.success(true)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun getLogs(baseUrl: String): Result<List<String>> = withContext(Dispatchers.IO) {
        try {
            val url = "$baseUrl/api/logs"
            val request = Request.Builder().url(url).get().build()
            client.newCall(request).execute().use { response ->
                if (response.isSuccessful) {
                    val body = response.body?.string() ?: ""
                    val type = object : TypeToken<List<String>>() {}.type
                    val logs = gson.fromJson<List<String>>(body, type)
                    Result.success(logs)
                } else {
                    Result.failure(Exception("HTTP ${response.code}"))
                }
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
