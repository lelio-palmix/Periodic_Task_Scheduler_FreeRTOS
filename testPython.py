import subprocess

# Lancia QEMU normalmente (senza aspettare GDB) e cattura l'output
processo = subprocess.Popen(
    ["make", "qemu_start"], # Assumendo esista questo target nel Makefile
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

# Legge i log della vostra UART
for linea in processo.stdout:
    print(f"Letto da QEMU: {linea.strip()}")
    # Qui inserisci la logica: if "DEADLINE_MISS" in linea, allora...