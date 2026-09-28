import threading, time, wave, socket, tkinter as tk, asyncio, edge_tts, requests, base64, random, queue, os, subprocess, tempfile
from tkinter import scrolledtext
from faster_whisper import WhisperModel
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry
from openai import OpenAI
import sounddevice as sd
import numpy as np
from enum import Enum

class EstadoUli(Enum): INACTIVO="INACTIVO"; ESCUCHANDO="ESCUCHANDO"; PROCESANDO="PROCESANDO"; HABLANDO="HABLANDO"
class Prio(Enum): URGENTE=0; ACCION=1; NORMAL=2; FONDO=3
COMANDOS_VALIDOS = {"QUIETO", "HABLAR", "PARPADEAR", "OREJAS_ADELANTE", "ROTAR_IZQUIERDA", "ROTAR_DERECHA"}

ESP32_HOSTNAME = "animatronico.local"
DEFAULT_FALLBACK_IP = "192.168.137.203"
OPENAI_API_KEY = "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"

def obtener_ip_esp32():
    try: return socket.gethostbyname(ESP32_HOSTNAME)
    except socket.gaierror: return DEFAULT_FALLBACK_IP

ESP32_IP = obtener_ip_esp32()
client = OpenAI(api_key=OPENAI_API_KEY)
whisper_model = WhisperModel("base", device="cpu", compute_type="int8", cpu_threads=4, num_workers=1)

def transcribir_audio(wav_path):
    segments, _ = whisper_model.transcribe(wav_path, language="es", task="transcribe", beam_size=1, best_of=1, temperature=0.0, vad_filter=True, vad_parameters={"min_silence_duration_ms": 250, "speech_pad_ms": 250}, condition_on_previous_text=False, initial_prompt="Español colombiano. Uli, animatrónico de la Universidad Libre.")
    return " ".join(s.text.strip() for s in segments).strip()

class AnimatronicTotalApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Cerebro Uli - Streaming TTS / Baja Latencia")
        self.root.geometry("1000x780")
        self.root.config(bg="#1e1e1e")
        self.esp32_ip = ESP32_IP
        self.estado = EstadoUli.INACTIVO
        self.comando_actual = "QUIETO"
        self.is_monitoring = False 

        self.session = requests.Session()
        retries = Retry(total=2, backoff_factor=0.1, status_forcelist=[500, 502, 503, 504])
        self.session.mount("http://", HTTPAdapter(max_retries=retries, pool_connections=10, pool_maxsize=10))

        self.command_queue = queue.PriorityQueue()
        self.interaction_queue = queue.Queue(maxsize=1)
        
        threading.Thread(target=self.controlador_comandos, daemon=True).start()
        threading.Thread(target=self.trabajador_interacciones, daemon=True).start()
        threading.Thread(target=self.bucle_movimientos_fondo, daemon=True).start()

        titulo = tk.Label(root, text="🤖 Panel Uli (Streaming & Métricas Reales)", font=("Arial", 14, "bold"), bg="#1e1e1e", fg="#00ffcc")
        titulo.pack(pady=10)
        main_frame = tk.Frame(root, bg="#1e1e1e")
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        left_frame = tk.Frame(main_frame, bg="#1e1e1e")
        left_frame.pack(side=tk.LEFT, fill=tk.Y, expand=False, padx=5)

        ctrl_frame = tk.LabelFrame(left_frame, text=" Control IA ", font=("Arial", 10, "bold"), bg="#2d2d2d", fg="white")
        ctrl_frame.pack(fill=tk.X, pady=5, ipadx=10, ipady=5)
        
        self.btn_monitor = tk.Button(ctrl_frame, text="🔊 Escuchar en Vivo", bg="#17a2b8", fg="white", width=24, command=self.toggle_monitor_mic)
        self.btn_monitor.pack(pady=4)
        self.btn_mic = tk.Button(ctrl_frame, text="🎙️ Encender Cerebro IA", bg="#28a745", fg="white", width=24, command=self.toggle_mic)
        self.btn_mic.pack(pady=4)
        tk.Button(ctrl_frame, text="📸 Foto y Describir", bg="#ffc107", fg="black", width=24, command=self.accion_analizar_foto_manual).pack(pady=4)

        lat_frame = tk.LabelFrame(left_frame, text=" Latencia Real (ms) ", font=("Arial", 10, "bold"), bg="#1a1a1a", fg="#ff5555")
        lat_frame.pack(fill=tk.X, pady=10, ipadx=10, ipady=5)
        self.lbl_lat_voz = tk.Label(lat_frame, text="Captura voz: -- s", bg="#1a1a1a", fg="#aaaaaa", font=("Consolas", 10)); self.lbl_lat_voz.pack(anchor="w", padx=5)
        self.lbl_lat_whisper = tk.Label(lat_frame, text="Whisper:     -- s", bg="#1a1a1a", fg="#aaaaaa", font=("Consolas", 10)); self.lbl_lat_whisper.pack(anchor="w", padx=5)
        self.lbl_lat_ia = tk.Label(lat_frame, text="OpenAI:      -- s", bg="#1a1a1a", fg="#aaaaaa", font=("Consolas", 10)); self.lbl_lat_ia.pack(anchor="w", padx=5)
        self.lbl_lat_tts = tk.Label(lat_frame, text="1er Audio:   -- s", bg="#1a1a1a", fg="#aaaaaa", font=("Consolas", 10)); self.lbl_lat_tts.pack(anchor="w", padx=5)
        self.lbl_lat_total = tk.Label(lat_frame, text="RESPUESTA:   -- s", bg="#1a1a1a", fg="#00ffcc", font=("Consolas", 10, "bold")); self.lbl_lat_total.pack(anchor="w", padx=5, pady=5)
        self.lbl_lat_habla = tk.Label(lat_frame, text="Dur. HABLA:  -- s", bg="#1a1a1a", fg="#aaaaaa", font=("Consolas", 10)); self.lbl_lat_habla.pack(anchor="w", padx=5, pady=(0, 5))

        right_frame = tk.LabelFrame(main_frame, text=" Registro en Vivo ", font=("Arial", 10, "bold"), bg="#2d2d2d", fg="white")
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.log_area = scrolledtext.ScrolledText(right_frame, wrap=tk.WORD, font=("Consolas", 10), bg="#121212", fg="#00ff00")
        self.log_area.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.log(f"Sistema listo. IP: {self.esp32_ip}")

    def actualizar_metricas(self, voz=None, whisper=None, ia=None, tts=None, respuesta=None, habla=None):
        if voz is not None: self.lbl_lat_voz.config(text=f"Captura voz: {voz:.2f} s")
        if whisper is not None: self.lbl_lat_whisper.config(text=f"Whisper:     {whisper:.2f} s")
        if ia is not None: self.lbl_lat_ia.config(text=f"OpenAI:      {ia:.2f} s")
        if tts is not None: self.lbl_lat_tts.config(text=f"1er Audio:   {tts:.2f} s")
        if respuesta is not None: self.lbl_lat_total.config(text=f"RESPUESTA:   {respuesta:.2f} s")
        if habla is not None: self.lbl_lat_habla.config(text=f"Dur. HABLA:  {habla:.2f} s")

    def purgar_fondo(self):
        respaldo = []
        try:
            while True:
                item = self.command_queue.get_nowait()
                if item[0] < Prio.FONDO.value: respaldo.append(item)
                self.command_queue.task_done()
        except queue.Empty: pass
        for item in respaldo: self.command_queue.put(item)

    def controlador_comandos(self):
        while True:
            prioridad, _, comando = self.command_queue.get()
            if prioridad <= Prio.ACCION.value: self.purgar_fondo()
            if prioridad == Prio.FONDO.value and self.estado == EstadoUli.HABLANDO:
                self.command_queue.task_done(); continue
            if comando != self.comando_actual or comando in ["PARPADEAR", "HABLAR"]:
                try:
                    self.session.post(f"http://{self.esp32_ip}/comando", data=comando, timeout=1.0)
                    if comando not in ["PARPADEAR"]: self.comando_actual = comando
                except Exception: pass
            self.command_queue.task_done()

    def enviar_comando(self, comando, prioridad=Prio.NORMAL):
        self.command_queue.put((prioridad.value, time.perf_counter(), comando))

    def bucle_movimientos_fondo(self):
        while True:
            time.sleep(random.uniform(8.0, 15.0))
            if self.estado in [EstadoUli.ESCUCHANDO, EstadoUli.INACTIVO] and self.comando_actual == "QUIETO":
                self.enviar_comando("PARPADEAR", prioridad=Prio.FONDO)
            time.sleep(random.uniform(5.0, 10.0))
            if self.estado in [EstadoUli.ESCUCHANDO, EstadoUli.INACTIVO] and self.comando_actual == "QUIETO" and random.random() > 0.5:
                self.enviar_comando(random.choice(["ROTAR_IZQUIERDA", "ROTAR_DERECHA"]), prioridad=Prio.FONDO)
                time.sleep(random.uniform(1.5, 2.5))
                self.enviar_comando("QUIETO", prioridad=Prio.FONDO)

    def log(self, mensaje):
        self.root.after(0, lambda: [self.log_area.insert(tk.END, f"{mensaje}\n"), self.log_area.see(tk.END)])

    def toggle_monitor_mic(self):
        self.is_monitoring = not self.is_monitoring
        self.btn_monitor.config(text="🔕 Detener Escucha" if self.is_monitoring else "🔊 Escuchar en Vivo", bg="#d9534f" if self.is_monitoring else "#17a2b8")
        if self.is_monitoring: threading.Thread(target=self.bucle_monitoreo_audio, daemon=True).start()

    def toggle_mic(self):
        if self.estado == EstadoUli.INACTIVO:
            self.estado = EstadoUli.ESCUCHANDO
            self.btn_mic.config(text="🛑 Apagar Cerebro IA", bg="#dc3545")
            self.log("\n[CEREBRO IA] Activado.")
            threading.Thread(target=self.bucle_escucha, daemon=True).start()
        else:
            self.estado = EstadoUli.INACTIVO
            self.btn_mic.config(text="🎙️ Encender Cerebro IA", bg="#28a745")

    def accion_analizar_foto_manual(self):
        if self.estado != EstadoUli.ESCUCHANDO:
            self.log("[SISTEMA] Uli está ocupado. Espera a que termine.")
            return
        def tarea():
            self.estado = EstadoUli.PROCESANDO
            self.log("[CÁMARA] Capturando imagen...")
            imagen = self.obtener_foto_esp32()
            if not imagen:
                self.log("[CÁMARA] Error al obtener imagen.")
                self.estado = EstadoUli.ESCUCHANDO; return
            inicio = time.perf_counter()
            respuesta = self.consultar_openai("Describe brevemente lo que ves.", imagen)
            tiempo = time.perf_counter() - inicio
            self.log(f"[VISIÓN] ({tiempo:.2f}s): {respuesta}")
            cmd = "QUIETO"; txt = respuesta
            if "[" in respuesta and "]" in respuesta:
                partes = respuesta.split("]", 1)
                cmd_raw = partes[0].replace("[", "").strip()
                txt = partes[1].strip()
                cmd = cmd_raw if cmd_raw in COMANDOS_VALIDOS else "QUIETO"
            self.reproducir_audio_y_animar(cmd, txt, inicio)
        threading.Thread(target=tarea, daemon=True).start()

    def obtener_foto_esp32(self):
        try:
            r = self.session.get(f"http://{self.esp32_ip}/camara.jpg", timeout=3.0)
            if r.status_code == 200: return r.content
        except Exception: pass
        return None

    def reproducir_audio_y_animar(self, comando_gestual, texto_respuesta, inicio_global):
        self.estado = EstadoUli.HABLANDO  
        self.enviar_comando(comando_gestual, prioridad=Prio.URGENTE)
        self.enviar_comando("HABLAR", prioridad=Prio.ACCION)
        
        inicio_tts = time.perf_counter()
        primer_chunk_recibido = False
        tiempo_inicio_habla = 0
        
        async def generar_tts_streaming():
            nonlocal primer_chunk_recibido, tiempo_inicio_habla
            proceso = None
            try:
                proceso = subprocess.Popen(["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", "-i", "pipe:0"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, bufsize=0)
                com = edge_tts.Communicate(texto_respuesta, "es-CO-SalomeNeural", pitch="+20Hz", rate="+15%")
                buffer_audio = bytearray()
                async for chunk in com.stream():
                    if chunk["type"] == "audio":
                        buffer_audio.extend(chunk["data"])
                        if not primer_chunk_recibido:
                            primer_chunk_recibido = True
                            tiempo_inicio_habla = time.perf_counter()
                            tiempo_tts = tiempo_inicio_habla - inicio_tts
                            latencia_respuesta = tiempo_inicio_habla - inicio_global
                            self.root.after(0, lambda: self.actualizar_metricas(tts=tiempo_tts, respuesta=latencia_respuesta))
                            if proceso and proceso.stdin:
                                proceso.stdin.write(buffer_audio)
                                proceso.stdin.flush()
                            buffer_audio.clear()
                        elif len(buffer_audio) >= 4096:
                            if proceso and proceso.stdin:
                                proceso.stdin.write(buffer_audio)
                                proceso.stdin.flush()
                            buffer_audio.clear()
                if buffer_audio and proceso and proceso.stdin:
                    proceso.stdin.write(buffer_audio)
                    proceso.stdin.flush()
            except Exception as e:
                self.log(f"[ERROR TTS STREAM]: {e}")
            finally:
                if proceso is not None:
                    try:
                        if proceso.stdin: proceso.stdin.close()
                    except Exception: pass
                    try: proceso.wait(timeout=3)
                    except subprocess.TimeoutExpired: proceso.kill()
                if primer_chunk_recibido:
                    duracion_habla = time.perf_counter() - tiempo_inicio_habla
                    self.root.after(0, lambda: self.actualizar_metricas(habla=duracion_habla))

        try: asyncio.run(generar_tts_streaming())
        except Exception as e: self.log(f"[ERROR TTS]: {e}")
        finally:
            # CORRECCIÓN: Detención inmediata al terminar el audio (sin sleep previo)
            self.enviar_comando("QUIETO", prioridad=Prio.NORMAL)
            if self.estado != EstadoUli.INACTIVO: self.estado = EstadoUli.ESCUCHANDO  

    def consultar_openai(self, texto_usuario, imagen_bytes=None):
        prompt_sistema = "Eres Uli, un zorro animatrónico de la Universidad Libre. Responde alegre y natural. MÁXIMO 2 frases. Inicia SIEMPRE con UNO de estos comandos: [QUIETO], [HABLAR], [PARPADEAR], [OREJAS_ADELANTE], [ROTAR_IZQUIERDA], [ROTAR_DERECHA]."
        mensajes = [{"role": "system", "content": prompt_sistema}]
        if imagen_bytes:
            imagen_b64 = base64.b64encode(imagen_bytes).decode('utf-8')
            mensajes.append({"role": "user", "content": [{"type": "text", "text": texto_usuario}, {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{imagen_b64}"}}]})
        else:
            mensajes.append({"role": "user", "content": texto_usuario})
        try:
            resp = client.chat.completions.create(messages=mensajes, model="gpt-4o-mini", temperature=0.6, max_tokens=50)
            return resp.choices[0].message.content.strip()
        except Exception: return "[QUIETO] Tuve un error de red."

    def procesar_interaccion(self, texto_usuario, inicio_global):
        self.log(f"\n🗣️ [USUARIO]: {texto_usuario}") 
        texto_min = texto_usuario.lower()
        comandos_directos = {"parpadea": "PARPADEAR", "gira a la derecha": "ROTAR_DERECHA", "gira a la izquierda": "ROTAR_IZQUIERDA"}
        for clave, comando in comandos_directos.items():
            if clave in texto_min:
                self.enviar_comando(comando, prioridad=Prio.URGENTE)
                self.log(f"⚡ [ACCIÓN LOCAL]: {comando}")
                def finalizar_accion_local():
                    self.enviar_comando("QUIETO", prioridad=Prio.NORMAL)
                    if self.estado != EstadoUli.INACTIVO: self.estado = EstadoUli.ESCUCHANDO
                threading.Timer(2.0, finalizar_accion_local).start()
                return

        quiere_foto = any(p in texto_min for p in ["qué ves", "que ves", "mira", "observa", "describe"]) 
        imagen = self.obtener_foto_esp32() if quiere_foto else None 
        inicio_ia = time.perf_counter()
        respuesta_ia = self.consultar_openai(texto_usuario, imagen) 
        tiempo_ia = time.perf_counter() - inicio_ia
        
        self.root.after(0, lambda: self.actualizar_metricas(ia=tiempo_ia))
        self.log(f"🤖 [IA] ({tiempo_ia:.2f}s): {respuesta_ia}") 

        cmd = "QUIETO"; txt = respuesta_ia 
        if "[" in respuesta_ia and "]" in respuesta_ia:
            partes = respuesta_ia.split("]", 1) 
            cmd_raw = partes[0].replace("[", "").strip() 
            txt = partes[1].strip() 
            cmd = cmd_raw if cmd_raw in COMANDOS_VALIDOS else "QUIETO"
            
        self.reproducir_audio_y_animar(cmd, txt, inicio_global)

    def trabajador_interacciones(self):
        while True:
            datos = self.interaction_queue.get()
            wav_path, inicio_global, tiempo_voz = datos['wav_path'], datos['inicio_global'], datos['tiempo_voz']
            if self.estado == EstadoUli.INACTIVO:
                try: os.remove(wav_path)
                except: pass
                self.interaction_queue.task_done(); continue

            if self.estado == EstadoUli.ESCUCHANDO: self.estado = EstadoUli.PROCESANDO
            try:
                inicio_whisper = time.perf_counter()
                txt = transcribir_audio(wav_path)
                tiempo_whisper = time.perf_counter() - inicio_whisper
                self.root.after(0, lambda: self.actualizar_metricas(voz=tiempo_voz, whisper=tiempo_whisper))
                if txt:
                    if "uli" in txt.lower() or "hola" in txt.lower():
                        self.enviar_comando("OREJAS_ADELANTE", prioridad=Prio.URGENTE)
                        self.procesar_interaccion(txt, inicio_global)
                    else:
                        self.log(f"[IGNORADO]: {txt}")
                        if self.estado == EstadoUli.PROCESANDO: self.estado = EstadoUli.ESCUCHANDO
            except Exception as e:
                self.log(f"[ERROR TRABAJADOR]: {e}")
                if self.estado == EstadoUli.PROCESANDO: self.estado = EstadoUli.ESCUCHANDO
            finally:
                try: os.remove(wav_path)
                except: pass
                self.interaction_queue.task_done()

    def bucle_monitoreo_audio(self):
        url_audio = f"http://{self.esp32_ip}/audio.wav"
        try:
            with sd.OutputStream(samplerate=16000, channels=1, dtype="int16") as stream:
                while self.is_monitoring:
                    if self.estado in [EstadoUli.PROCESANDO, EstadoUli.HABLANDO]: 
                        time.sleep(0.1); continue
                    try:
                        with self.session.get(url_audio, stream=True, timeout=3.0) as r:
                            if r.status_code != 200: continue
                            for chunk in r.iter_content(chunk_size=2048):
                                if not self.is_monitoring or self.estado in [EstadoUli.PROCESANDO, EstadoUli.HABLANDO]: break
                                if not chunk: continue
                                if len(chunk) % 2 != 0: chunk = chunk[:-1]
                                if not chunk: continue
                                audio_data = np.frombuffer(chunk, dtype=np.int16)
                                if len(audio_data) > 0: stream.write(audio_data)
                    except Exception: time.sleep(0.5)
        except Exception:
            self.is_monitoring = False
            self.root.after(0, lambda: self.btn_monitor.config(text="🔊 Escuchar en Vivo", bg="#17a2b8"))

    def bucle_escucha(self):
        url_audio = f"http://{self.esp32_ip}/audio.wav" 
        bytes_por_segundo = 32000 
        while self.estado != EstadoUli.INACTIVO:
            try:
                if self.estado != EstadoUli.ESCUCHANDO: 
                    time.sleep(0.1); continue
                with self.session.get(url_audio, stream=True, timeout=4.0) as r: 
                    if r.status_code != 200: continue
                    audio_buffer = bytearray(); grabando = False; silence_bytes = 0; cabecera_pendiente = 44; inicio_captura = 0
                    for chunk in r.iter_content(chunk_size=4096): 
                        if self.estado != EstadoUli.ESCUCHANDO: break
                        if cabecera_pendiente > 0:
                            descartar = min(cabecera_pendiente, len(chunk))
                            chunk = chunk[descartar:]
                            cabecera_pendiente -= descartar
                        if not chunk: continue
                        if len(chunk) % 2 != 0: chunk = chunk[:-1]
                        if not chunk: continue

                        audio_original = np.frombuffer(chunk, dtype=np.int16)
                        volumen = np.abs(audio_original).mean() if len(audio_original) > 0 else 0 
                        audio_amplificado = np.clip(audio_original.astype(np.float32) * 1.5, -32768, 32767).astype(np.int16)
                        
                        if volumen > 250:
                            if not grabando: inicio_captura = time.perf_counter()
                            grabando = True; silence_bytes = 0 
                            audio_buffer.extend(audio_amplificado.tobytes()) 
                        elif grabando:
                            silence_bytes += len(audio_original.tobytes())
                            audio_buffer.extend(audio_amplificado.tobytes()) 
                            if silence_bytes > (bytes_por_segundo * 0.4): break
                        if len(audio_buffer) >= 160 * 1024: break 

                    if grabando and len(audio_buffer) > 8000 and self.estado == EstadoUli.ESCUCHANDO:
                        tiempo_voz = time.perf_counter() - inicio_captura
                        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp_wav: wav_path = tmp_wav.name
                        with wave.open(wav_path, "wb") as wf: 
                            wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(16000); wf.writeframes(audio_buffer) 
                        try:
                            self.interaction_queue.put_nowait({'wav_path': wav_path, 'inicio_global': inicio_captura, 'tiempo_voz': tiempo_voz})
                        except queue.Full:
                            self.log("[AUDIO] Cola llena. Interacción descartada.")
                            try: os.remove(wav_path)
                            except OSError: pass
            except Exception: time.sleep(0.5) 

if __name__ == "__main__":
    root = tk.Tk() 
    app = AnimatronicTotalApp(root) 
    root.mainloop()