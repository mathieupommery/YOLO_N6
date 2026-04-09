import csv
import re
import math
import sys

def parse_ibis_val(val_str):
    """
    Convertit une chaîne IBIS avec son unité (ex: '2.5nH', '1.2e-09') en un float (en unités de base).
    """
    if val_str.upper() == 'NA':
        return None
        
    match = re.match(r"([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)([a-zA-Z]*)", val_str.strip())
    if not match:
        return None
    
    num_str, suffix = match.groups()
    num = float(num_str)
    suffix = suffix.lower()
    
    if suffix.startswith('m'): num *= 1e-3
    elif suffix.startswith('u'): num *= 1e-6
    elif suffix.startswith('n'): num *= 1e-9
    elif suffix.startswith('p'): num *= 1e-12
    elif suffix.startswith('f'): num *= 1e-15
    
    return num

def extract_ibis_delays(ibis_file_path, csv_file_path):
    l_pkg_typ = None
    c_pkg_typ = None
    
    pins_info = {}  # Stocke le nom du signal pour chaque pin
    l_dict = {}     # Stocke l'inductance de chaque pin issue de la matrice
    c_dict = {}     # Stocke la capacité de chaque pin issue de la matrice
    
    in_pin_section = False
    current_matrix = None
    current_row_pin = None
    
    try:
        # ÉTAPE 1 : Parsing du fichier
        with open(ibis_file_path, 'r', encoding='utf-8') as f:
            for line in f:
                original_line = line.strip()
                
                # Suppression des commentaires
                if '|' in original_line:
                    line = original_line.split('|')[0].strip()
                else:
                    line = original_line
                    
                if not line:
                    continue
                    
                # Détection des sections
                if line.startswith('['):
                    section_name = line.split(']')[0].upper().strip() + ']'
                    
                    if section_name == '[PIN]':
                        in_pin_section = True
                        current_matrix = None
                        continue
                    elif section_name == '[INDUCTANCE MATRIX]':
                        current_matrix = 'L'
                        in_pin_section = False
                        continue
                    elif section_name == '[CAPACITANCE MATRIX]':
                        current_matrix = 'C'
                        in_pin_section = False
                        continue
                    elif section_name == '[RESISTANCE MATRIX]':
                        current_matrix = 'R'
                        in_pin_section = False
                        continue
                    elif section_name == '[PACKAGE]':
                        current_matrix = 'PKG'
                        in_pin_section = False
                        continue
                    elif section_name == '[ROW]':
                        # Ne pas changer de matrice, c'est juste une ligne de donnée
                        pass
                    else:
                        in_pin_section = False
                        if 'MATRIX' not in section_name:
                            current_matrix = None
                
                # Récupération valeurs globales [Package]
                if current_matrix == 'PKG':
                    if line.upper().startswith('L_PKG'):
                        parts = line.split()
                        if len(parts) >= 2: l_pkg_typ = parse_ibis_val(parts[1])
                    elif line.upper().startswith('C_PKG'):
                        parts = line.split()
                        if len(parts) >= 2: c_pkg_typ = parse_ibis_val(parts[1])

                # Récupération de la section [Pin] (format simple)
                if in_pin_section:
                    parts = line.split()
                    if len(parts) >= 3 and not line.startswith('['):
                        pin_name = parts[0]
                        signal_name = parts[1]
                        
                        l_val = parse_ibis_val(parts[4]) if len(parts) >= 5 else None
                        c_val = parse_ibis_val(parts[5]) if len(parts) >= 6 else None
                        
                        pins_info[pin_name] = {
                            'signal': signal_name,
                            'L': l_val,
                            'C': c_val
                        }
                        
                # Récupération des Matrices Avancées (Sparse Matrix)
                if current_matrix in ['L', 'C']:
                    if line.upper().startswith('[ROW]'):
                        parts = line.split()
                        if len(parts) >= 2:
                            current_row_pin = parts[1]
                    
                    if current_row_pin:
                        # Nettoyer la ligne du tag [Row] pour faciliter la lecture
                        clean_line = re.sub(r'(?i)\[ROW\]', '', line).strip()
                        tokens = clean_line.split()
                        
                        # On cherche la valeur diagonale : NomDuPin suivi de sa valeur
                        for i in range(len(tokens) - 1):
                            if tokens[i] == current_row_pin:
                                val = parse_ibis_val(tokens[i+1])
                                if val is not None:
                                    if current_matrix == 'L':
                                        if current_row_pin not in l_dict: l_dict[current_row_pin] = val
                                    elif current_matrix == 'C':
                                        if current_row_pin not in c_dict: c_dict[current_row_pin] = val
                                    break

        # ÉTAPE 2 : Traitement et Calcul
        pins_data = []
        for pin, info in pins_info.items():
            signal = info['signal']
            
            # Priorité 1: Valeur de la Matrice. Priorité 2: Valeur de la section Pin. Priorité 3: Valeur globale
            l_val = l_dict.get(pin, info['L'])
            if l_val is None or l_val == 0: l_val = l_pkg_typ
                
            c_val = c_dict.get(pin, info['C'])
            if c_val is None or c_val == 0: c_val = c_pkg_typ
                
            if l_val is not None and c_val is not None and l_val > 0 and c_val > 0:
                delay_seconds = math.sqrt(l_val * c_val)
                
                pins_data.append({
                    'Pin': pin,
                    'Signal': signal,
                    'Inductance_nH': round(l_val * 1e9, 3),
                    'Capacitance_pF': round(c_val * 1e12, 3),
                    'Delay_ps': round(delay_seconds * 1e12, 3)
                })

        # ÉTAPE 3 : Écriture CSV
        with open(csv_file_path, 'w', newline='', encoding='utf-8') as csvfile:
            fieldnames = ['Pin', 'Signal', 'Inductance_nH', 'Capacitance_pF', 'Delay_ps']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            
            writer.writeheader()
            for row in pins_data:
                writer.writerow(row)
                
        print(f"✅ Extraction réussie !")
        print(f"   Broches trouvées dans [Pin] : {len(pins_info)}")
        print(f"   Valeurs L trouvées : {len(l_dict)} | Valeurs C trouvées : {len(c_dict)}")
        print(f"   Broches exportées avec succès : {len(pins_data)}")
        print(f"   Fichier généré : {csv_file_path}")

    except Exception as e:
        print(f"❌ Une erreur s'est produite : {e}")

# === Lancement du script ===
if __name__ == "__main__":
    fichier_entree_ibis = "stm32n657_vfbga178.ibs"
    fichier_sortie_csv = "delais_pins_stm32n657.csv"
    
    print(f"Analyse du fichier '{fichier_entree_ibis}' en cours...")
    extract_ibis_delays(fichier_entree_ibis, fichier_sortie_csv)