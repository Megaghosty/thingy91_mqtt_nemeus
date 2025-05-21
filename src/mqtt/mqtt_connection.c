#include <stdio.h>
#include <string.h>
#include <ncs_version.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
//#include <nrf_modem_at.h>

#include <modem/modem_key_mgmt.h>

#include <dk_buttons_and_leds.h>
#include "mqtt_connection.h"
#include "../datatypes/datatypes.h"


#if NCS_VERSION_NUMBER < 0x20600
#include <zephyr/random/rand32.h>
#else
#include <zephyr/random/random.h>
#endif

extern device_shadow_t g_device_state;
static struct mqtt_client client;
static struct sockaddr_storage broker;

/* Buffers for MQTT client. */
static uint8_t rx_buffer[1024];
static uint8_t tx_buffer[1024];
static uint8_t payload_buf[256];

/* MQTT Broker details. */
static struct sockaddr_storage broker;

LOG_MODULE_DECLARE(nrf9160_mqtt_gnss);

/**@brief Function to get the payload of recived data.
 */
static int get_received_payload(struct mqtt_client *c, size_t length)
{
	int ret;
	int err = 0;

	/* Return an error if the payload is larger than the payload buffer.
	 * Note: To allow new messages, we have to read the payload before returning.
	 */
	if (length > sizeof(payload_buf))
	{
		err = -EMSGSIZE;
	}

	/* Truncate payload until it fits in the payload buffer. */
	while (length > sizeof(payload_buf))
	{
		ret = mqtt_read_publish_payload_blocking(
			c, payload_buf, (length - sizeof(payload_buf)));
		if (ret == 0)
		{
			return -EIO;
		}
		else if (ret < 0)
		{
			return ret;
		}

		length -= ret;
	}

	ret = mqtt_readall_publish_payload(c, payload_buf, length);
	if (ret)
	{
		return ret;
	}

	return err;
}

/**@brief Function to subscribe to the configured topic
 */
static int subscribe(struct mqtt_client *const c)
{
	struct mqtt_topic subscribe_topic = {
		.topic = {
			.utf8 = MQTT_TOPIC,
			.size = strlen(MQTT_TOPIC)},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE};

	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic,
		.list_count = 1,
		.message_id = 1234};

	LOG_INF("Subscribing to: %s len %u", MQTT_TOPIC,
			(unsigned int)strlen(MQTT_TOPIC));

	return mqtt_subscribe(c, &subscription_list);
}

/**@brief Function to print strings without null-termination
 */
static void data_print(uint8_t *prefix, uint8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	LOG_INF("%s%s", (char *)prefix, (char *)buf);
}

/**@brief Function to publish data on the configured topic
 */
int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
				 uint8_t *data, size_t len)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = MQTT_TOPIC;
	param.message.topic.topic.size = strlen(MQTT_TOPIC);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s len: %u",
			MQTT_TOPIC,
			(unsigned int)strlen(MQTT_TOPIC));

	return mqtt_publish(c, &param);
}
/**@brief MQTT client event handler
 */
void mqtt_evt_handler(struct mqtt_client *const c,
					  const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type)
	{
	case MQTT_EVT_CONNACK:
		/* Subscribe to the topic MQTT_TOPIC when we have a successful connection */
		if (evt->result != 0)
		{
			LOG_ERR("MQTT connect failed: %d", evt->result);
			break;
		}

		LOG_INF("MQTT client connected");
		subscribe(c);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected: %d", evt->result);
		break;

	case MQTT_EVT_PUBLISH:
		/* Listen to published messages received from the broker and extract the message */
		{
			/* Extract the payload */
			const struct mqtt_publish_param *p = &evt->param.publish;
			// Print the length of the recived message
			LOG_INF("MQTT PUBLISH result=%d len=%d",
					evt->result, p->message.payload.len);

			// Extract the data of the recived message
			err = get_received_payload(c, p->message.payload.len);

			// Send acknowledgment to the broker on receiving QoS1 publish message
			if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
			{
				const struct mqtt_puback_param ack = {
					.message_id = p->message_id};

				/* Send acknowledgment. */
				mqtt_publish_qos1_ack(c, &ack);
			}

			/* On successful extraction of data */
			if (err >= 0)
			{
				data_print("Received: ", payload_buf, p->message.payload.len);
				// Control the LED
				if (strncmp(payload_buf, CONFIG_TURN_LED_ON_CMD, sizeof(CONFIG_TURN_LED_ON_CMD) - 1) == 0)
				{
					dk_set_led_on(LED_CONTROL_OVER_MQTT);
					g_device_state.led1_state = true;
				}
				else if (strncmp(payload_buf, CONFIG_TURN_LED_OFF_CMD, sizeof(CONFIG_TURN_LED_OFF_CMD) - 1) == 0)
				{
					dk_set_led_off(LED_CONTROL_OVER_MQTT);
					g_device_state.led1_state = false;
				}
				/* On failed extraction of data */
				// Payload buffer is smaller than the received data
			}
			else if (err == -EMSGSIZE)
			{
				LOG_ERR("Received payload (%d bytes) is larger than the payload buffer size (%d bytes).",
						p->message.payload.len, sizeof(payload_buf));
				// Failed to extract data, disconnect
			}
			else
			{
				LOG_ERR("get_received_payload failed: %d", err);
				LOG_INF("Disconnecting MQTT client...");

				err = mqtt_disconnect(c);
				if (err)
				{
					LOG_ERR("Could not disconnect: %d", err);
				}
			}
		}
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT SUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PINGRESP error: %d", evt->result);
		}
		break;

	default:
		LOG_INF("Unhandled MQTT event type: %d", evt->type);
		break;
	}
}

/**@brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM};

	err = getaddrinfo(MQTT_BROKER_ADDR, NULL, &hints, &result);
	if (err)
	{
		LOG_ERR("getaddrinfo failed: %d", err);
		return -ECHILD;
	}

	addr = result;

	/* Look for address of the broker. */
	while (addr != NULL)
	{
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in))
		{
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
					->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
					  ipv4_addr, sizeof(ipv4_addr));
			LOG_INF("IPv4 Address found %s", (char *)(ipv4_addr));

			break;
		}
		else
		{
			LOG_ERR("ai_addrlen = %u should be %u or %u",
					(unsigned int)addr->ai_addrlen,
					(unsigned int)sizeof(struct sockaddr_in),
					(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
	}

	/* Free the address. */
	freeaddrinfo(result);

	return err;
}

/* Function to get the client id */
static const uint8_t *client_id_get(void)
{
	static uint8_t client_id[MAX(sizeof(CONFIG_MQTT_CLIENT_ID),
								 CLIENT_ID_LEN)];

	if (strlen(CONFIG_MQTT_CLIENT_ID) > 0)
	{
		snprintf(client_id, sizeof(client_id), "%s",
				 CONFIG_MQTT_CLIENT_ID);
		goto exit;
	}

	char imei_buf[CGSN_RESPONSE_LENGTH + 1];
	int err;

	err = nrf_modem_at_cmd(imei_buf, sizeof(imei_buf), "AT+CGSN");
	if (err)
	{
		LOG_ERR("Failed to obtain IMEI, error: %d", err);
		goto exit;
	}

	imei_buf[IMEI_LEN] = '\0';

	snprintf(client_id, sizeof(client_id), "nrf-%.*s", IMEI_LEN, imei_buf);

exit:
	LOG_DBG("client_id = %s", (char *)(client_id));

	return client_id;
}

/**@brief Initialize the MQTT client structure
 */
/* Define the function client_init() to initialize the MQTT client instance.  */
int client_init(struct mqtt_client *client)
{
	int err;
	/* Initializes the client instance. */
	mqtt_client_init(client);

	/* Resolves the configured hostname and initializes the MQTT broker structure */
	err = broker_init();
	if (err)
	{
		LOG_ERR("Failed to initialize broker connection");
		return err;
	}

	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;
	static sec_tag_t sec_tag_list[] = { CONFIG_MQTT_TLS_SEC_TAG };

	client->transport.type = MQTT_TRANSPORT_SECURE;

    tls_config->peer_verify = CONFIG_PEER_VERIFY;
    tls_config->cipher_count = 0;
 	tls_config->cipher_list = NULL;
    tls_config->sec_tag_count = ARRAY_SIZE(sec_tag_list);
    tls_config->sec_tag_list = sec_tag_list;
    tls_config->hostname = CONFIG_MQTT_BROKER_HOSTNAME;

	/* MQTT client configuration */
    client->broker = &broker;
    client->evt_cb = NULL;
    client->client_id.utf8 = (uint8_t *)"thingy91_client";
    client->client_id.size = strlen("thingy91_client");
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

	return err;
}

/**@brief Initialize the file descriptor structure used by poll.
 */
int fds_init(struct mqtt_client *c, struct pollfd *fds)
{
	if (c->transport.type == MQTT_TRANSPORT_SECURE)
	{
		fds->fd = c->transport.tls.sock;
	}
	else
	{
		return -ENOTSUP;
	}

	fds->events = POLLIN;

	return 0;
}