# Thingy91_mqtt_NEMEIS

Exemple minimaliste de collecte et publication de données capteurs + GNSS via MQTT sur Nordic Thingy:91 utilisé pour un projet
**nRF Connect SDK v2.7.0**

---

## Présentation

Ce projet permet de :
- Surveiller l’état de la LED1
- Lire le pourcentage de batterie (via PMIC ADP5360)
- Mesurer la qualité de l’air (capteur BME680)
- Récupérer la position GNSS

L’application se connecte à un broker MQTT de votre choix, publie automatiquement l’état du device toutes les **30 secondes** et permet le contrôle de la LED1 à distance.  
Ce code est volontairement épuré pour faciliter la compréhension et l’expérimentation avec le Thingy:91.

---

## Fonctionnalités

- **Publication automatique** : toutes les 30 secondes, l’état du device (LED, batterie, air, GNSS) est publié en JSON sur le topic MQTT configuré.
- **Envoi manuel** : un appui sur le bouton du Thingy:91 force la publication immédiate.
- **Contrôle LED1** : publiez `LED1ON` ou `LED1OFF` sur le topic de souscription pour piloter la LED1 à distance.
- **Logs détaillés** : suivez la connexion LTE, les coordonnées GNSS et les échanges MQTT via le port série.
- **Configuration flexible** : topics, endpoint MQTT et timings configurables dans `prj.conf` et Kconfig.

---

## Prérequis

- Nordic Thingy:91 avec SIM LTE-M/NB-IoT fonctionnelle
- nRF Connect SDK v2.7.0 installé
- Outils `west`, `nrfjprog`, etc.
- Accès à un broker MQTT (public ou privé)
- Logiciel client MQTT (MQTT Explorer, mosquitto_sub, etc.)

---

## Configuration

1. **Cloner le dépôt**
git clone <url_du_repo>
cd hingy91_mqtt_simple



2. **Configurer les topics et l’endpoint MQTT**
- Modifier `prj.conf` pour l’URL du broker.
- Adapter les topics dans Kconfig (`CONFIG_MQTT_PUB_TOPIC` et `CONFIG_MQTT_SUB_TOPIC`).

3. **Configurer les timings**
- Le délai entre deux publications automatiques est paramétré à 30 secondes par défaut (voir Kconfig).

---

## Compilation

Pour Thingy:91 :
west build -b thingy91/nrf9160/ns -p auto



Pour nRF9160-DK :
west build -b nrf9160dk/nrf9160/ns -p auto



---

## Flash et utilisation

1. **Flasher le firmware**
west flash



2. **Démarrer le Thingy:91**
- Le device se connecte automatiquement au réseau LTE puis au broker MQTT.
- Surveillez les logs série pour suivre l’état de la connexion et les messages envoyés.

3. **Publier une commande LED**
- Pour allumer la LED1 : publier `LED1ON` sur le topic de souscription.
- Pour éteindre la LED1 : publier `LED1OFF`.

4. **Recevoir les données**
- Les messages JSON sont publiés toutes les 30 secondes sur le topic configuré.
- Un appui sur le bouton force l’envoi immédiat du message JSON.

---

## Exemple de message JSON publié

{
"led1": 1,
"battery": 92,
"air_quality": 43,
"lat": 48.8566,
"lon": 2.3522,
"alt": 35
}


*Remarque : Avant le premier fix GNSS, lat/lon/alt sont à 0.*

---

## Architecture du code

| Module    | Fonction principale                                         |
|-----------|------------------------------------------------------------|
| main      | Initialisation et logique principale de connexion          |
| mqtt      | Gestion de la connexion MQTT                               |
| gnss      | Configuration modem et récupération GNSS                   |
| pmic      | Initialisation PMIC et lecture périodique batterie         |
| datatypes | Structure des données système et génération JSON           |
| sensors   | Lecture du capteur BME680 (qualité de l’air)               |

---

## Remarques techniques

- Ce code utilise principalement des variables globales pour la clarté.  
  Pour la production, privilégiez les primitives de synchronisation (zbus, message queues, sémaphores).
- La construction du message JSON est manuelle pour limiter l’empreinte mémoire.
- Pour ajouter la sécurité TLS, référez-vous à la [Nordic Developer Academy](https://academy.nordicsemi.com).

---

## Ressources

- [Nordic Developer Academy – Cellular fundamentals](https://academy.nordicsemi.com)
- [Documentation SYS_INIT Zephyr](https://docs.zephyrproject.org/latest/develop/runtime_init.html)

---
