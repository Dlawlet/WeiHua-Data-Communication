"""
Script de test de performance pour programmes C++ avec limitation de ressources
Permet de définir des limites de CPU, RAM et timeout, et mesure les performances
"""

import subprocess
import psutil
import time
import json
import sys
import os
import threading
import platform
from datetime import datetime
from pathlib import Path


class ResourceLimiter:
    """Classe pour exécuter un programme avec des limitations de ressources"""
    
    def __init__(self, executable_path, config_path=None):
        """
        Args:
            executable_path: Chemin vers l'exécutable à tester
            config_path: Chemin vers le fichier de configuration (optionnel)
        """
        self.executable_path = Path(executable_path)
        self.config = self._load_config(config_path) if config_path else self._default_config()
        
        if not self.executable_path.exists():
            raise FileNotFoundError(f"Executable not found: {self.executable_path}")
    
    def _default_config(self):
        """Configuration par défaut
        
        Note: max_cpu_percent est mesuré par CORE (pas total système).
        Sur un système 4 cores:
        - 100% = utilisation complète d'1 core
        - 400% = utilisation complète de tous les cores
        """
        return {
            "timeout_seconds": 10,
            "max_memory_mb": 512,
            "max_cpu_percent": 25,
            "input_file": None,
            "output_file": None,
            "check_interval": 0.1  # Interval de vérification en secondes
        }
    
    def _load_config(self, config_path):
        """Charge la configuration depuis un fichier JSON"""
        with open(config_path, 'r') as f:
            config = json.load(f)
        # Merge avec la config par défaut
        default = self._default_config()
        default.update(config)
        return default
    
    def _apply_cpu_limits(self, ps_process, verbose):
        """Applique les limites CPU selon le système d'exploitation"""
        system = platform.system()
        cpu_limit = self.config['max_cpu_percent']
        
        try:
            if system == 'Linux':
                # Sous Linux: Utiliser nice pour réduire la priorité
                # nice va de -20 (haute priorité) à 19 (basse priorité)
                if cpu_limit < 50:
                    nice_value = 10  # Basse priorité
                elif cpu_limit < 80:
                    nice_value = 5   # Priorité moyennement réduite
                else:
                    nice_value = 0   # Priorité normale
                
                ps_process.nice(nice_value)
                
                if verbose and nice_value > 0:
                    print(f"🐧 Linux: CPU priority reduced (nice={nice_value})")
                
                # Alternative: Utiliser cpulimit si disponible
                # cpulimit_cmd = f"cpulimit -p {ps_process.pid} -l {cpu_limit} &"
                # subprocess.Popen(cpulimit_cmd, shell=True)
                
            elif system == 'Windows':
                # Sous Windows: Utiliser les classes de priorité
                if cpu_limit < 50:
                    ps_process.nice(psutil.IDLE_PRIORITY_CLASS)
                    if verbose:
                        print(f"🪟 Windows: Process priority set to IDLE")
                elif cpu_limit < 80:
                    ps_process.nice(psutil.BELOW_NORMAL_PRIORITY_CLASS)
                    if verbose:
                        print(f"🪟 Windows: Process priority set to BELOW_NORMAL")
                # Sinon, laisser en NORMAL
                
            elif system == 'Darwin':  # macOS
                # macOS supporte nice comme Linux
                if cpu_limit < 50:
                    ps_process.nice(10)
                elif cpu_limit < 80:
                    ps_process.nice(5)
                    
        except (psutil.NoSuchProcess, psutil.AccessDenied, PermissionError) as e:
            if verbose:
                print(f"⚠️  Cannot set CPU limits: {e}")
    
    def run_with_limits(self, input_data=None, verbose=True):
        """
        Exécute le programme avec les limitations configurées
        
        Args:
            input_data: Données d'entrée (str) ou chemin vers un fichier
            verbose: Afficher les informations détaillées
            
        Returns:
            dict: Résultats de l'exécution avec métriques
        """
        results = {
            "success": False,
            "timeout": False,
            "memory_exceeded": False,
            "cpu_exceeded": False,
            "execution_time": 0,
            "max_memory_used_mb": 0,
            "avg_cpu_percent": 0,
            "return_code": None,
            "stdout": "",
            "stderr": "",
            "error": None
        }
        
        try:
            # Préparer l'entrée
            stdin_data = self._prepare_input(input_data)
            
            # Démarrer le processus
            start_time = time.time()
            
            process = subprocess.Popen(
                [str(self.executable_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # CORRECTION: Gérer stdin/stdout/stderr dans des threads pour éviter les deadlocks
            # Les pipes ont une taille limitée - si on ne lit pas, le programme se bloque!
            stdout_data = []
            stderr_data = []
            
            def write_stdin():
                try:
                    if stdin_data:
                        process.stdin.write(stdin_data)
                        process.stdin.flush()
                    process.stdin.close()
                except (BrokenPipeError, OSError):
                    pass
            
            def read_stdout():
                try:
                    for line in process.stdout:
                        stdout_data.append(line)
                except:
                    pass
            
            def read_stderr():
                try:
                    for line in process.stderr:
                        stderr_data.append(line)
                except:
                    pass
            
            # Démarrer les threads
            stdin_thread = threading.Thread(target=write_stdin)
            stdout_thread = threading.Thread(target=read_stdout)
            stderr_thread = threading.Thread(target=read_stderr)
            
            stdin_thread.daemon = True
            stdout_thread.daemon = True
            stderr_thread.daemon = True
            
            stdin_thread.start()
            stdout_thread.start()
            stderr_thread.start()
            
            # Obtenir l'objet psutil pour monitorer les ressources
            try:
                ps_process = psutil.Process(process.pid)
            except psutil.NoSuchProcess:
                results["error"] = "Process terminated immediately"
                return results
            
            # Monitoring des ressources
            memory_samples = []
            cpu_samples = []
            cpu_exceeded_count = 0  # Compteur pour éviter les faux positifs
            
            # Appliquer les limites CPU selon le système d'exploitation
            self._apply_cpu_limits(ps_process, verbose)
            
            if verbose:
                num_cores = psutil.cpu_count(logical=True)
                print(f"🚀 Starting process (PID: {process.pid})...")
                print(f"⏱️  Timeout: {self.config['timeout_seconds']}s")
                print(f"💾 Max Memory: {self.config['max_memory_mb']} MB")
                print(f"🖥️  Max CPU: {self.config['max_cpu_percent']}% per core (System: {num_cores} cores)")
                print("-" * 60)
            
            # Boucle de monitoring
            while True:
                # Vérifier si le processus est terminé
                if process.poll() is not None:
                    break
                
                # Vérifier le timeout
                elapsed = time.time() - start_time
                if elapsed > self.config['timeout_seconds']:
                    results["timeout"] = True
                    process.terminate()
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        process.kill()
                    if verbose:
                        print(f"⏰ TIMEOUT after {elapsed:.2f}s")
                    break
                
                # Mesurer les ressources
                try:
                    # Mémoire
                    mem_info = ps_process.memory_info()
                    memory_mb = mem_info.rss / (1024 * 1024)
                    memory_samples.append(memory_mb)
                    
                    # CPU (nécessite un intervalle pour être précis)
                    cpu_percent = ps_process.cpu_percent(interval=None)
                    if cpu_percent > 0:  # Ignorer les valeurs nulles initiales
                        cpu_samples.append(cpu_percent)
                    
                    # Vérifier les limites de mémoire
                    if memory_mb > self.config['max_memory_mb']:
                        results["memory_exceeded"] = True
                        process.terminate()
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            process.kill()
                        if verbose:
                            print(f"💥 MEMORY LIMIT EXCEEDED: {memory_mb:.2f} MB")
                        break
                    
                    # Vérifier et limiter le CPU
                    # Note: La limitation stricte du CPU sous Windows est complexe
                    # On utilise une approche de "kill si dépassement prolongé"
                    if cpu_percent > self.config['max_cpu_percent'] * 1.5:  # Tolérance de 50%
                        cpu_exceeded_count += 1
                        if cpu_exceeded_count >= 10:  # 10 échantillons consécutifs = 1 seconde
                            results["cpu_exceeded"] = True
                            process.terminate()
                            try:
                                process.wait(timeout=2)
                            except subprocess.TimeoutExpired:
                                process.kill()
                            if verbose:
                                print(f"💥 CPU LIMIT EXCEEDED: {cpu_percent:.1f}% > {self.config['max_cpu_percent']}%")
                            break
                    else:
                        cpu_exceeded_count = 0  # Reset si le CPU redescend
                    
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    break
                
                time.sleep(self.config['check_interval'])
            
            # Attendre la fin des threads de lecture
            stdout_thread.join(timeout=2)
            stderr_thread.join(timeout=2)
            
            # Récupérer les données lues par les threads
            results["stdout"] = ''.join(stdout_data)
            results["stderr"] = ''.join(stderr_data)
            
            # Calculer les métriques
            results["execution_time"] = time.time() - start_time
            results["return_code"] = process.returncode
            
            if memory_samples:
                results["max_memory_used_mb"] = max(memory_samples)
            
            if cpu_samples:
                results["avg_cpu_percent"] = sum(cpu_samples) / len(cpu_samples)
            
            # Déterminer le succès
            results["success"] = (
                process.returncode == 0 and
                not results["timeout"] and
                not results["memory_exceeded"]
            )
            
            if verbose:
                self._print_results(results)
            
            # Écrire stdout dans un fichier si spécifié
            if results["success"] and self.config.get('output_file'):
                output_path = self.config['output_file']
                with open(output_path, 'w') as f:
                    f.write(results["stdout"])
                if verbose:
                    print(f"📄 Output written to: {output_path}")
            
        except Exception as e:
            results["error"] = str(e)
            if verbose:
                print(f"❌ Error: {e}")
        
        return results
    
    def _prepare_input(self, input_data):
        """Prépare les données d'entrée"""
        if input_data is None:
            # Utiliser le fichier d'entrée de la config si spécifié
            if self.config.get('input_file'):
                with open(self.config['input_file'], 'r') as f:
                    return f.read()
            return None
        
        # Si c'est un chemin de fichier
        if isinstance(input_data, (str, Path)) and Path(input_data).exists():
            with open(input_data, 'r') as f:
                return f.read()
        
        # Sinon, traiter comme une chaîne
        return str(input_data)
    
    def _print_results(self, results):
        """Affiche les résultats de manière formatée"""
        print("-" * 60)
        print("📊 RESULTS:")
        print(f"  ✓ Success: {results['success']}")
        print(f"  ⏱️  Execution Time: {results['execution_time']:.3f}s")
        print(f"  💾 Max Memory Used: {results['max_memory_used_mb']:.2f} MB")
        print(f"  🖥️  Avg CPU: {results['avg_cpu_percent']:.1f}%")
        print(f"  🔢 Return Code: {results['return_code']}")
        
        if results['timeout']:
            print("  ⏰ TIMEOUT!")
        if results['memory_exceeded']:
            print("  💥 MEMORY LIMIT EXCEEDED!")
        if results['error']:
            print(f"  ❌ Error: {results['error']}")
        
        print("-" * 60)
    
    def save_results(self, results, output_path):
        """Sauvegarde les résultats dans un fichier JSON"""
        results_with_metadata = {
            "timestamp": datetime.now().isoformat(),
            "executable": str(self.executable_path),
            "config": self.config,
            "results": results
        }
        
        with open(output_path, 'w') as f:
            json.dump(results_with_metadata, f, indent=2)
        
        print(f"💾 Results saved to: {output_path}")


def main():
    """Exemple d'utilisation"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Test C++ program with resource limits")
    parser.add_argument("executable", help="Path to the executable")
    parser.add_argument("-c", "--config", help="Path to config file (JSON)")
    parser.add_argument("-i", "--input", help="Path to input file")
    parser.add_argument("-O", "--output-file", help="Path to write program stdout")
    parser.add_argument("-o", "--output", help="Path to save results (JSON)")
    parser.add_argument("-t", "--timeout", type=float, help="Timeout in seconds")
    parser.add_argument("-m", "--memory", type=int, help="Max memory in MB")
    parser.add_argument("--cpu", type=int, help="Max CPU percent")
    
    args = parser.parse_args()
    
    # Créer le limiter
    limiter = ResourceLimiter(args.executable, args.config)
    
    # Override config with command line arguments
    if args.timeout is not None:
        limiter.config['timeout_seconds'] = args.timeout
    if args.memory is not None:
        limiter.config['max_memory_mb'] = args.memory
    if args.cpu is not None:
        limiter.config['max_cpu_percent'] = args.cpu
    if args.input is not None:
        limiter.config['input_file'] = args.input
    if args.output_file is not None:
        limiter.config['output_file'] = args.output_file
    
    # Exécuter
    results = limiter.run_with_limits()
    
    # Sauvegarder si demandé
    if args.output:
        limiter.save_results(results, args.output)
    
    # Code de sortie basé sur le succès
    sys.exit(0 if results['success'] else 1)


if __name__ == "__main__":
    main()
