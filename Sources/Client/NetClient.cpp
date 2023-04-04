/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <math.h>
#include <string.h>
#include <vector>

#include <enet/enet.h>

#include "CTFGameMode.h"
#include "Client.h"
#include "GameMap.h"
#include "GameMapLoader.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "NetClient.h"
#include "Player.h"
#include "TCGameMode.h"
#include "Weapon.h"
#include "World.h"
#include <Core/CP437.h>
#include <Core/Debug.h>
#include <Core/DeflateStream.h>
#include <Core/Exception.h>
#include <Core/Math.h>
#include <Core/MemoryStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/TMPUtils.h>

DEFINE_SPADES_SETTING(cg_unicode, "1");
DEFINE_SPADES_SETTING(cg_DemoRecord, "1");

namespace spades {
	namespace client {

		namespace {
			const char UTFSign = -1;

			enum { BLUE_FLAG = 0, GREEN_FLAG = 1, BLUE_BASE = 2, GREEN_BASE = 3 };
			enum PacketType {
				PacketTypePositionData = 0,			// C2S2P
				PacketTypeOrientationData = 1,		// C2S2P
				PacketTypeWorldUpdate = 2,			// S2C
				PacketTypeInputData = 3,			// C2S2P
				PacketTypeWeaponInput = 4,			// C2S2P
				PacketTypeHitPacket = 5,			// C2S
				PacketTypeSetHP = 5,				// S2C
				PacketTypeGrenadePacket = 6,		// C2S2P
				PacketTypeSetTool = 7,				// C2S2P
				PacketTypeSetColour = 8,			// C2S2P
				PacketTypeExistingPlayer = 9,		// C2S2P
				PacketTypeShortPlayerData = 10,		// S2C
				PacketTypeMoveObject = 11,			// S2C
				PacketTypeCreatePlayer = 12,		// S2C
				PacketTypeBlockAction = 13,			// C2S2P
				PacketTypeBlockLine = 14,			// C2S2P
				PacketTypeStateData = 15,			// S2C
				PacketTypeKillAction = 16,			// S2C
				PacketTypeChatMessage = 17,			// C2S2P
				PacketTypeMapStart = 18,			// S2C
				PacketTypeMapChunk = 19,			// S2C
				PacketTypePlayerLeft = 20,			// S2P
				PacketTypeTerritoryCapture = 21,	// S2P
				PacketTypeProgressBar = 22,			// S2P
				PacketTypeIntelCapture = 23,		// S2P
				PacketTypeIntelPickup = 24,			// S2P
				PacketTypeIntelDrop = 25,			// S2P
				PacketTypeRestock = 26,				// S2P
				PacketTypeFogColour = 27,			// S2C
				PacketTypeWeaponReload = 28,		// C2S2P
				PacketTypeChangeTeam = 29,			// C2S2P
				PacketTypeChangeWeapon = 30,		// C2S2P
				PacketTypeMapCached = 31,			// S2C
				PacketTypeHandShakeInit = 31,		// S2C
				PacketTypeHandShakeReturn = 32,		// C2S
				PacketTypeVersionGet = 33,			// S2C
				PacketTypeVersionSend = 34,			// C2S
				PacketTypeExtensionInfo = 60,
			};

			enum class VersionInfoPropertyId : std::uint8_t {
				ApplicationNameAndVersion = 0,
				UserLocale = 1,
				ClientFeatureFlags1 = 2
			};

			enum class ClientFeatureFlags1 : std::uint32_t { None = 0, SupportsUnicode = 1 << 0 };

			ClientFeatureFlags1 operator|(ClientFeatureFlags1 a, ClientFeatureFlags1 b) {
				return (ClientFeatureFlags1)((uint32_t)a | (uint32_t)b);
			}
			ClientFeatureFlags1& operator|=(ClientFeatureFlags1& a, ClientFeatureFlags1 b) {
				return a = a | b;
			}

			std::string EncodeString(std::string str) {
				auto str2 = CP437::Encode(str, -1);
				if (!cg_unicode)
					return str2; // ignore fallbacks

				// some fallbacks; always use UTF8
				if (str2.find(-1) != std::string::npos)
					str.insert(0, &UTFSign, 1);
				else
					str = str2;

				return str;
			}

			std::string DecodeString(std::string s) {
				if (s.size() > 0 && s[0] == UTFSign)
					return s.substr(1);

				return CP437::Decode(s);
			}
		} // namespace

		class NetPacketReader {
			std::vector<char> data;
			size_t pos;

		public:
			NetPacketReader(ENetPacket* packet) {
				SPADES_MARK_FUNCTION();

				data.resize(packet->dataLength);
				memcpy(data.data(), packet->data, packet->dataLength);
				enet_packet_destroy(packet);
				pos = 1;
			}

			NetPacketReader(const std::vector<char> inData) {
				data = inData;
				pos = 1;
			}

			unsigned int GetTypeRaw() { return static_cast<unsigned int>(data[0]); }
			PacketType GetType() { return static_cast<PacketType>(GetTypeRaw()); }

			uint32_t ReadInt() {
				SPADES_MARK_FUNCTION();

				uint32_t value = 0;
				if (pos + 4 > data.size())
					SPRaise("Received packet truncated");

				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 16;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 24;
				return value;
			}

			uint16_t ReadShort() {
				SPADES_MARK_FUNCTION();

				uint32_t value = 0;
				if (pos + 2 > data.size())
					SPRaise("Received packet truncated");

				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				return (uint16_t)value;
			}

			uint8_t ReadByte() {
				SPADES_MARK_FUNCTION();

				if (pos >= data.size())
					SPRaise("Received packet truncated");

				return (uint8_t)data[pos++];
			}

			float ReadFloat() {
				SPADES_MARK_FUNCTION();
				union {
					float f;
					uint32_t v;
				};
				v = ReadInt();
				return f;
			}

			IntVector3 ReadIntColor() {
				SPADES_MARK_FUNCTION();
				IntVector3 col;
				col.z = ReadByte(); // B
				col.y = ReadByte(); // G
				col.x = ReadByte(); // R
				return col;
			}
			IntVector3 ReadIntVector3() {
				SPADES_MARK_FUNCTION();
				IntVector3 v;
				v.x = ReadInt();
				v.y = ReadInt();
				v.z = ReadInt();
				return v;
			}
			Vector3 ReadVector3() {
				SPADES_MARK_FUNCTION();
				Vector3 v;
				v.x = ReadFloat();
				v.y = ReadFloat();
				v.z = ReadFloat();
				return v;
			}

			std::size_t GetPosition() { return data.size(); }
			std::size_t GetNumRemainingBytes() { return data.size() - pos; }
			std::vector<char> GetData() { return data; }

			std::string ReadData(size_t siz) {
				if (pos + siz > data.size())
					SPRaise("Received packet truncated");

				std::string s = std::string(data.data() + pos, siz);
				pos += siz;
				return s;
			}
			std::string ReadRemainingData() {
				return std::string(data.data() + pos, data.size() - pos);
			}

			std::string ReadString(size_t siz) {
				SPADES_MARK_FUNCTION_DEBUG();
				// convert to C string once so that null-chars are removed
				return DecodeString(ReadData(siz).c_str());
			}
			std::string ReadRemainingString() {
				SPADES_MARK_FUNCTION_DEBUG();
				// convert to C string once so that null-chars are removed
				return DecodeString(ReadRemainingData().c_str());
			}

			void DumpDebug() {
#if 1
				char buf[1024];
				std::string str;
				sprintf(buf, "Packet 0x%02x [len=%d]", (int)GetType(), (int)data.size());
				str = buf;
				int bytes = (int)data.size();
				if (bytes > 64)
					bytes = 64;

				for (int i = 0; i < bytes; i++) {
					sprintf(buf, " %02x", (unsigned int)(unsigned char)data[i]);
					str += buf;
				}

				SPLog("%s", str.c_str());
#endif
			}
		};

		class NetPacketWriter {
			std::vector<char> data;

		public:
			NetPacketWriter(PacketType type) { data.push_back(type); }

			void WriteByte(uint8_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back(v);
			}
			void WriteShort(uint16_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
			}
			void WriteInt(uint32_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
				data.push_back((char)(v >> 16));
				data.push_back((char)(v >> 24));
			}
			void WriteFloat(float v) {
				SPADES_MARK_FUNCTION_DEBUG();
				union {
					float f;
					uint32_t i;
				};
				f = v;
				WriteInt(i);
			}

			void WriteColor(IntVector3 v) {
				WriteByte((uint8_t)v.z); // B
				WriteByte((uint8_t)v.y); // G
				WriteByte((uint8_t)v.x); // R
			}
			void WriteIntVector3(IntVector3 v) {
				WriteInt((uint32_t)v.x);
				WriteInt((uint32_t)v.y);
				WriteInt((uint32_t)v.z);
			}
			void WriteVector3(const Vector3& v) {
				WriteFloat(v.x);
				WriteFloat(v.y);
				WriteFloat(v.z);
			}

			void WriteString(std::string str) {
				str = EncodeString(str);
				data.insert(data.end(), str.begin(), str.end());
			}

			void WriteString(const std::string& str, size_t fillLen) {
				WriteString(str.substr(0, fillLen));
				size_t sz = str.size();
				while (sz < fillLen) {
					WriteByte((uint8_t)0);
					sz++;
				}
			}

			std::size_t GetPosition() { return data.size(); }

			void Update(std::size_t position, std::uint8_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position >= data.size()) {
					SPRaise("Invalid write (%d should be less than %d)",
						(int)position, (int)data.size());
				}

				data[position] = static_cast<char>(newValue);
			}

			void Update(std::size_t position, std::uint32_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position + 4 > data.size()) {
					SPRaise("Invalid write (%d should be less than or equal to %d)",
					        (int)(position + 4), (int)data.size());
				}

				// Assuming the target platform is little endian and supports
				// unaligned memory access...
				*reinterpret_cast<std::uint32_t*>(data.data() + position) = newValue;
			}

			ENetPacket* CreatePacket(int flag = ENET_PACKET_FLAG_RELIABLE) {
				return enet_packet_create(data.data(), data.size(), flag);
			}
		};

		NetClient::NetClient(Client* c, bool replay) : client(c), host(nullptr), peer(nullptr) {
			SPADES_MARK_FUNCTION();

			if (!replay) {
				enet_initialize();
				SPLog("ENet initialized");
	
				host = enet_host_create(NULL, 1, 1, 100000, 100000);
				SPLog("ENet host created");
				if (!host)
					SPRaise("Failed to create ENet host");
	
				if (enet_host_compress_with_range_coder(host) < 0)
					SPRaise("Failed to enable ENet Range coder.");

				SPLog("ENet Range Coder Enabled");
			}

			peer = NULL;
			status = NetClientStatusNotConnected;

			lastPlayerInput = 0;
			lastWeaponInput = 0;

			savedPlayerPos.resize(128);
			savedPlayerFront.resize(128);
			savedPlayerTeam.resize(128);

			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			if (!replay) {
				bandwidthMonitor.reset(new BandwidthMonitor(host));
			}
		}
		NetClient::~NetClient() {
			SPADES_MARK_FUNCTION();

			Disconnect();
			if (host)
				enet_host_destroy(host);
			bandwidthMonitor.reset();
			SPLog("ENet host destroyed");
			DemoStop();
		}

		struct Demo CurrentDemo;
		struct Demo ResetStruct;

		void NetClient::Connect(const ServerAddress& hostname) {
			SPADES_MARK_FUNCTION();

			Disconnect();
			SPAssert(status == NetClientStatusNotConnected);

			switch (hostname.GetProtocolVersion()) {
				case ProtocolVersion::v075:
					SPLog("Using Ace of Spades 0.75 protocol");
					protocolVersion = 3;
					break;
				case ProtocolVersion::v076:
					SPLog("Using Ace of Spades 0.76 protocol");
					protocolVersion = 4;
					break;
				default: SPRaise("Invalid ProtocolVersion"); break;
			}

			ENetAddress addr = hostname.GetENetAddress();
			SPLog("Connecting to %u:%u", (unsigned int)addr.host, (unsigned int)addr.port);

			savedPackets.clear();

			peer = enet_host_connect(host, &addr, 1, protocolVersion);
			if (peer == NULL)
				SPRaise("Failed to create ENet peer");

			properties.reset(new GameProperties(hostname.GetProtocolVersion()));

			status = NetClientStatusConnecting;
			statusString = _Tr("NetClient", "Connecting to the server");
		}

		void NetClient::Disconnect() {
			SPADES_MARK_FUNCTION();

			if (!peer)
				return;

			enet_peer_disconnect(peer, 0);
			status = NetClientStatusNotConnected;
			statusString = _Tr("NetClient", "Not connected");

			savedPackets.clear();

			ENetEvent event;
			SPLog("Waiting for graceful disconnection");
			while (enet_host_service(host, &event, 1000) > 0) {
				switch (event.type) {
					case ENET_EVENT_TYPE_RECEIVE:
						enet_packet_destroy(event.packet); break;
					case ENET_EVENT_TYPE_DISCONNECT:
						// disconnected safely
						// FIXME: release peer
						enet_peer_reset(peer);
						peer = NULL;
						return;
					default:;
						// discard
				}
			}

			SPLog("Connection terminated");
			enet_peer_reset(peer);
			// FXIME: release peer
			peer = NULL;
		}

		int NetClient::GetPing() {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return -1;

			auto rtt = peer->roundTripTime;
			if (rtt == 0)
				return -1;
			return static_cast<int>(rtt);
		}

		void NetClient::DoEvents(int timeout) {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return;

			if (bandwidthMonitor)
				bandwidthMonitor->Update();

			ENetEvent event;
			while (enet_host_service(host, &event, timeout) > 0) {
				if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
					if (GetWorld())
						client->SetWorld(NULL);

					enet_peer_reset(peer);
					peer = NULL;
					status = NetClientStatusNotConnected;

					std::string reasonStr = DisconnectReasonString(event.data);
					SPLog("Disconnected (data = 0x%08x)", (unsigned int)event.data);
					statusString = "Disconnected: " + reasonStr;
					SPRaise("Disconnected: %s", reasonStr.c_str());
				}

				stmp::optional<NetPacketReader> readerOrNone;
				if (event.type == ENET_EVENT_TYPE_RECEIVE) {
					if (cg_DemoRecord && DemoStarted) {
						if (event.packet->data[0] != 15) {
							RegisterDemoPacket(event.packet);
						} else {
							int player_id = event.packet->data[1];
							event.packet->data[1] = 33;
							RegisterDemoPacket(event.packet);
							event.packet->data[1] = player_id;
						}
					} else if (DemoStarted) {
						DemoStop(); //stop if disable midgame. but cant enable midgame again
					}

					readerOrNone.reset(event.packet);
					auto& reader = readerOrNone.value();

					try {
						if (HandleHandshakePackets(reader))
							continue;
					} catch (const std::exception& ex) {
						int type = reader.GetType();
						reader.DumpDebug();
						SPRaise("Exception while handling packet type 0x%08x:\n%s", type, ex.what());
					}
				}

				if (status == NetClientStatusConnecting) {
					if (event.type == ENET_EVENT_TYPE_CONNECT) {
						statusString = _Tr("NetClient", "Awaiting for state");
					} else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();
						reader.DumpDebug();
						if (reader.GetType() != PacketTypeMapStart)
							SPRaise("Unexpected packet: %d", (int)reader.GetType());

						auto mapSize = reader.ReadInt();
						SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

						mapLoader.reset(new GameMapLoader());
						mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

						status = NetClientStatusReceivingMap;
						statusString = _Tr("NetClient", "Loading snapshot");
					}
				} else if (status == NetClientStatusReceivingMap) {
					SPAssert(mapLoader);

					if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();

						if (reader.GetType() == PacketTypeMapChunk) {
							std::vector<char> dt = reader.GetData();

							mapLoader->AddRawChunk(dt.data() + 1, dt.size() - 1);
							mapLoadMonitor->AccumulateBytes(
							  static_cast<unsigned int>(dt.size() - 1));
						} else {
							reader.DumpDebug();

							// The actual size of the map data cannot be known beforehand because
							// of compression. This means we must detect the end of the map
							// transfer in another way.
							//
							// We do this by checking for a StateData packet, which is sent
							// directly after the map transfer completes.
							//
							// A number of other packets can also be received while loading the map:
							//
							//  - World update packets (WorldUpdate, ExistingPlayer, and
							//    CreatePlayer) for the current round. We must store such packets
							//    temporarily and process them later when a `World` is created.
							//
							//  - Leftover reload packet from the previous round. This happens when
							//    you initiate the reload action and a map change occurs before it
							//    is completed. In pyspades, sending a reload packet is implemented
							//    by registering a callback function to the Twisted reactor. This
							//    callback function sends a reload packet, but it does not check if
							//    the current game round is finished, nor is it unregistered on a
							//    map change.
							//
							//    Such a reload packet would not (and should not) have any effect on
							//    the current round. Also, an attempt to process it would result in
							//    an "invalid player ID" exception, so we simply drop it during
							//    map load sequence.
							//

							if (reader.GetType() == PacketTypeStateData) {
								status = NetClientStatusConnected;
								statusString = _Tr("NetClient", "Connected");

								try {
									MapLoaded();
								} catch (const std::exception& ex) {
									if (strstr(ex.what(), "File truncated") ||
									    strstr(ex.what(), "EOF reached")) {
										SPLog("Map decoder returned error:\n%s", ex.what());
										Disconnect();
										statusString = _Tr("NetClient", "Error");
										throw;
									}
								} catch (...) {
									Disconnect();
									statusString = _Tr("NetClient", "Error");
									throw;
								}

								HandleGamePacket(reader);
							} else if (reader.GetType() == PacketTypeWeaponReload) {
								// Drop the reload packet. Pyspades does not
								// cancel the reload packets on map change and
								// they would cause an error if we would
								// process them
							} else {
								// Save the packet for later
								savedPackets.push_back(reader.GetData());
							}
						}
					}
				} else if (status == NetClientStatusConnected) {
					if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();

						try {
							HandleGamePacket(reader);
						} catch (const std::exception& ex) {
							int type = reader.GetType();
							reader.DumpDebug();
							SPRaise("Exception while handling packet type 0x%08x:\n%s", type, ex.what());
						}
					}
				}
			}
		}

		stmp::optional<World&> NetClient::GetWorld() { return client->GetWorld(); }

		stmp::optional<Player&> NetClient::GetPlayerOrNull(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Invalid Player ID %d: No world", pId);
			if (pId < 0 || pId >= int(GetWorld()->GetNumPlayerSlots()))
				return NULL;
			return GetWorld()->GetPlayer(pId);
		}
		Player& NetClient::GetPlayer(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Invalid Player ID %d: No world", pId);
			if (pId < 0 || pId >= int(GetWorld()->GetNumPlayerSlots()))
				SPRaise("Invalid Player ID %d: Out of range", pId);
			if (!GetWorld()->GetPlayer(pId))
				SPRaise("Invalid Player ID %d: Doesn't exist", pId);
			return GetWorld()->GetPlayer(pId).value();
		}

		Player& NetClient::GetLocalPlayer() {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Failed to get local player: no world");
			if (!GetWorld()->GetLocalPlayer())
				SPRaise("Failed to get local player: no local player");
			return GetWorld()->GetLocalPlayer().value();
		}

		stmp::optional<Player&> NetClient::GetLocalPlayerOrNull() {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Failed to get local player: no world");
			return GetWorld()->GetLocalPlayer();
		}
		PlayerInput ParsePlayerInput(uint8_t bits) {
			PlayerInput inp;
			inp.moveForward = (bits & (1 << 0)) != 0;
			inp.moveBackward = (bits & (1 << 1)) != 0;
			inp.moveLeft = (bits & (1 << 2)) != 0;
			inp.moveRight = (bits & (1 << 3)) != 0;
			inp.jump = (bits & (1 << 4)) != 0;
			inp.crouch = (bits & (1 << 5)) != 0;
			inp.sneak = (bits & (1 << 6)) != 0;
			inp.sprint = (bits & (1 << 7)) != 0;
			return inp;
		}

		WeaponInput ParseWeaponInput(uint8_t bits) {
			WeaponInput inp;
			inp.primary = ((bits & (1 << 0)) != 0);
			inp.secondary = ((bits & (1 << 1)) != 0);
			return inp;
		}

		std::string NetClient::DisconnectReasonString(uint32_t num) {
			switch (num) {
				case 1: return _Tr("NetClient", "You are banned from this server.");
				case 2: return _Tr("NetClient", "You have too many connections to this server.");
				case 3: return _Tr("NetClient", "Incompatible client protocol version.");
				case 4: return _Tr("NetClient", "Server full");
				case 10: return _Tr("NetClient", "You were kicked from this server.");
				default: return _Tr("NetClient", "Unknown Reason");
			}
		}

		bool NetClient::HandleHandshakePackets(spades::client::NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			switch (r.GetType()) {
				case PacketTypeHandShakeInit: SendHandShakeValid(r.ReadInt()); return true;
				case PacketTypeExtensionInfo: HandleExtensionPacket(r); return true;
				case PacketTypeVersionGet: {
					if (r.GetNumRemainingBytes() > 0) {
						// Enhanced variant
						std::set<std::uint8_t> propertyIds;
						while (r.GetNumRemainingBytes())
							propertyIds.insert(r.ReadByte());
						SendVersionEnhanced(propertyIds);
					} else {
						// Simple variant
						SendVersion();
					}
				}
					return true;
				default: return false;
			}
		}

		void NetClient::HandleExtensionPacket(spades::client::NetPacketReader& r) {
			int extCount = r.ReadByte();
			for (int i = 0; i < extCount; i++) {
				int extId = r.ReadByte();
				auto got = implementedExtensions.find(extId);
				if (got == implementedExtensions.end()) {
					SPLog("Client does not support extension %d", extId);
				} else {
					SPLog("Client supports extension %d", extId);
					extensions.emplace(got->first, got->second);
				}
			}

			SendSupportedExtensions();
		}

		void NetClient::HandleGamePacket(spades::client::NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			switch (r.GetType()) {
				case PacketTypePositionData: {
					Player& p = GetLocalPlayer();
					if (r.GetPosition() < 12) {
						// sometimes 00 00 00 00 packet is sent.
						// ignore this now
						break;
					}
					p.SetPosition(r.ReadVector3());
				} break;
				case PacketTypeOrientationData: {
					Player& p = GetLocalPlayer();
					p.SetOrientation(r.ReadVector3());
				} break;
				case PacketTypeWorldUpdate: {
					int bytesPerEntry = 24;
					if (protocolVersion == 4)
						bytesPerEntry++;

					client->MarkWorldUpdate();

					int entries = static_cast<int>(r.GetPosition() / bytesPerEntry);
					for (int i = 0; i < entries; i++) {
						int idx = i;
						if (protocolVersion == 4) {
							idx = r.ReadByte();
							if (idx < 0)
								SPRaise("Invalid player number %d received with WorldUpdate", idx);
						}

						Vector3 pos = r.ReadVector3();
						Vector3 front = r.ReadVector3();

						savedPlayerPos.at(idx) = pos;
						savedPlayerFront.at(idx) = front;

						{
							SPAssert(!pos.IsNaN());
							SPAssert(!front.IsNaN());
							SPAssert(front.GetLength() < 40.0F);

							if (GetWorld()) {
								auto p = GetWorld()->GetPlayer(idx);
								if (p && p != GetWorld()->GetLocalPlayer()
									&& p->IsAlive() && !p->IsSpectator()) {
									p->SetPosition(pos);
									p->SetOrientation(front);
								}
							}
						}
					}
					SPAssert(r.ReadRemainingData().empty());

					if (client->Replaying) {
						DemoCountUps();
					}
				} break;
				case PacketTypeInputData:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte();
						Player& p = GetPlayer(pId);

						PlayerInput inp = ParsePlayerInput(r.ReadByte());

						if (GetWorld()->GetLocalPlayer() == &p) {
							if (inp.jump) // handle "/fly" jump
								p.PlayerJump();

							break;
						}

						p.SetInput(inp);
					}
					break;
				case PacketTypeWeaponInput:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte();
						Player& p = GetPlayer(pId);

						WeaponInput inp = ParseWeaponInput(r.ReadByte());

						if (GetWorld()->GetLocalPlayer() == &p)
							break;

						p.SetWeaponInput(inp);
					}
					break;
				case PacketTypeSetHP: { // Hit Packet is Client-to-Server!
					Player& p = GetLocalPlayer();
					int hp = r.ReadByte();
					int type = r.ReadByte(); // 0=fall, 1=weap
					Vector3 source = r.ReadVector3();
					p.SetHP(hp, type ? HurtTypeWeapon : HurtTypeFall, source);
				} break;
				case PacketTypeGrenadePacket:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte(); // skip player Id
						float fuse = r.ReadFloat();
						Vector3 pos = r.ReadVector3();
						Vector3 vel = r.ReadVector3();
						Grenade* g = new Grenade(*GetWorld(), pos, vel, fuse);
						GetWorld()->AddGrenade(std::unique_ptr<Grenade>{g});
					}
					break;
				case PacketTypeSetTool: {
					Player& p = GetPlayer(r.ReadByte());
					int tool = r.ReadByte();
					switch (tool) {
						case 0: p.SetTool(Player::ToolSpade); break;
						case 1: p.SetTool(Player::ToolBlock); break;
						case 2: p.SetTool(Player::ToolWeapon); break;
						case 3: p.SetTool(Player::ToolGrenade); break;
						default: SPRaise("Received invalid tool type: %d", tool);
					}
				} break;
				case PacketTypeSetColour: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					IntVector3 color = r.ReadIntColor();
					if (p)
						p->SetHeldBlockColor(color);
					else
						temporaryPlayerBlockColor = color;
				} break;
				case PacketTypeExistingPlayer:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte();
						int team = r.ReadByte();
						int weapon = r.ReadByte();
						int tool = r.ReadByte();
						int kills = r.ReadInt();
						IntVector3 color = r.ReadIntColor();
						std::string name = r.ReadRemainingString();
						// TODO: decode name?

						WeaponType wType;
						switch (weapon) {
							case 0: wType = RIFLE_WEAPON; break;
							case 1: wType = SMG_WEAPON; break;
							case 2: wType = SHOTGUN_WEAPON; break;
							default: SPRaise("Received invalid weapon: %d", weapon);
						}

						auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team,
							savedPlayerPos[pId], GetWorld()->GetTeamColor(team));

						switch (tool) {
							case 0: p->SetTool(Player::ToolSpade); break;
							case 1: p->SetTool(Player::ToolBlock); break;
							case 2: p->SetTool(Player::ToolWeapon); break;
							case 3: p->SetTool(Player::ToolGrenade); break;
							default: SPRaise("Received invalid tool type: %d", tool);
						}
						p->SetHeldBlockColor(color);
						GetWorld()->SetPlayer(pId, std::move(p));

						World::PlayerPersistent& pers = GetWorld()->GetPlayerPersistent(pId);
						pers.name = name;
						pers.kills = kills;

						savedPlayerTeam[pId] = team;
					}
					break;
				case PacketTypeShortPlayerData:
					SPRaise("Unexpected: received Short Player Data");
				case PacketTypeMoveObject: {
					if (!GetWorld())
						SPRaise("No world");

					int type = r.ReadByte();
					int state = r.ReadByte();
					Vector3 pos = r.ReadVector3();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (mode && IGameMode::m_CTF == mode->ModeType()) {
						auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
						switch (type) {
							case BLUE_BASE: ctf.GetTeam(0).basePos = pos; break;
							case BLUE_FLAG: ctf.GetTeam(0).flagPos = pos; break;
							case GREEN_BASE: ctf.GetTeam(1).basePos = pos; break;
							case GREEN_FLAG: ctf.GetTeam(1).flagPos = pos; break;
						}
					} else if (mode && IGameMode::m_TC == mode->ModeType()) {
						auto& tc = dynamic_cast<TCGameMode&>(mode.value());
						if (type >= tc.GetNumTerritories()) {
								SPRaise("Invalid territory id specified: %d (max = %d)",
									(int)type, tc.GetNumTerritories() - 1);
						}

						if (state > 2)
							SPRaise("Invalid state %d specified for territory owner.", (int)state);

						TCGameMode::Territory& t = tc.GetTerritory(type);
						t.pos = pos;
						t.ownerTeamId = state;
					}
				} break;
				case PacketTypeCreatePlayer: {
					if (!GetWorld())
						SPRaise("No world");

					int pId = r.ReadByte();
					int weapon = r.ReadByte();
					int team = r.ReadByte();
					Vector3 pos = r.ReadVector3();
					pos.z -= 2.0F;
					std::string name = r.ReadRemainingString();
					// TODO: decode name?

					if (pId < 0) {
						SPLog("Ignoring invalid playerid %d (pyspades bug?: %s)", pId, name.c_str());
						break;
					}

					WeaponType wType;
					switch (weapon) {
						case 0: wType = RIFLE_WEAPON; break;
						case 1: wType = SMG_WEAPON; break;
						case 2: wType = SHOTGUN_WEAPON; break;
						default: SPRaise("Received invalid weapon: %d", weapon);
					}

					auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team,
						savedPlayerPos[pId], GetWorld()->GetTeamColor(team));
					p->SetPosition(pos);
					GetWorld()->SetPlayer(pId, std::move(p));

					Player& pRef = GetWorld()->GetPlayer(pId).value();
					World::PlayerPersistent& pers = GetWorld()->GetPlayerPersistent(pId);

					if (!name.empty()) // sometimes becomes empty
						pers.name = name;

					if (pId == GetWorld()->GetLocalPlayerIndex()) {
						client->LocalPlayerCreated();
						lastPlayerInput = 0xFFFFFFFF;
						lastWeaponInput = 0xFFFFFFFF;
						SendHeldBlockColor(); // ensure block color synchronized
					} else {
						if (savedPlayerTeam[pId] != team) {
							client->PlayerJoinedTeam(pRef);
							savedPlayerTeam[pId] = team;
						}
					}
					client->PlayerSpawned(pRef);
				} break;
				case PacketTypeBlockAction: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					int action = r.ReadByte();
					IntVector3 pos = r.ReadIntVector3();

					std::vector<IntVector3> cells;
					if (action == BlockActionCreate) {
						if (!p) {
							GetWorld()->CreateBlock(pos, temporaryPlayerBlockColor);
						} else {
							GetWorld()->CreateBlock(pos, p->GetBlockColor());
							client->PlayerCreatedBlock(*p);
							if (!GetWorld()->GetMap()->IsSolidWrapped(pos.x, pos.y, pos.z))
								p->UseBlocks(1);
						}
					} else if (action == BlockActionTool) {
						client->PlayerDestroyedBlockWithWeaponOrTool(pos);
						cells.push_back(pos);
						GetWorld()->DestroyBlock(cells);

						if (p && p->IsToolSpade())
							p->GotBlock();
					} else if (action == BlockActionDig) {
						client->PlayerDiggedBlock(pos);
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x, pos.y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
					} else if (action == BlockActionGrenade) {
						client->GrenadeDestroyedBlock(pos);
						for (int x = -1; x <= 1; x++)
						for (int y = -1; y <= 1; y++)
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x + x, pos.y + y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
					}
				} break;
				case PacketTypeBlockLine: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					IntVector3 pos1, pos2;
					pos1 = r.ReadIntVector3();
					pos2 = r.ReadIntVector3();

					auto cells = GetWorld()->CubeLine(pos1, pos2, 50);
					for (const auto& c : cells) {
						if (!GetWorld()->GetMap()->IsSolid(c.x, c.y, c.z))
							GetWorld()->CreateBlock(c, p ? p->GetBlockColor()
							                             : temporaryPlayerBlockColor);
					}

					if (p) {
						client->PlayerCreatedBlock(*p);
						p->UseBlocks(static_cast<int>(cells.size()));
					}
				} break;
				case PacketTypeStateData:
					if (!GetWorld())
						break;
					{
						// receives my player info.
						int pId = r.ReadByte();
						IntVector3 fogColor = r.ReadIntColor();

						IntVector3 teamColors[2];
						teamColors[0] = r.ReadIntColor();
						teamColors[1] = r.ReadIntColor();

						std::string teamNames[2];
						teamNames[0] = r.ReadString(10);
						teamNames[1] = r.ReadString(10);

						World::Team& t1 = GetWorld()->GetTeam(0);
						World::Team& t2 = GetWorld()->GetTeam(1);
						t1.color = teamColors[0];
						t2.color = teamColors[1];
						t1.name = teamNames[0];
						t2.name = teamNames[1];

						GetWorld()->SetFogColor(fogColor);
						GetWorld()->SetLocalPlayerIndex(pId);

						int mode = r.ReadByte();
						if (mode == CTFGameMode::m_CTF) { // CTF
							auto mode = stmp::make_unique<CTFGameMode>();

							CTFGameMode::Team& mt1 = mode->GetTeam(0);
							CTFGameMode::Team& mt2 = mode->GetTeam(1);

							mt1.score = r.ReadByte();
							mt2.score = r.ReadByte();
							mode->SetCaptureLimit(r.ReadByte());

							int intelFlags = r.ReadByte();
							mt1.hasIntel = (intelFlags & 1) != 0;
							mt2.hasIntel = (intelFlags & 2) != 0;

							if (mt2.hasIntel) {
								mt1.carrierId = r.ReadByte();
								r.ReadData(11);
							} else {
								mt1.flagPos = r.ReadVector3();
							}

							if (mt1.hasIntel) {
								mt2.carrierId = r.ReadByte();
								r.ReadData(11);
							} else {
								mt2.flagPos = r.ReadVector3();
							}

							mt1.basePos = r.ReadVector3();
							mt2.basePos = r.ReadVector3();

							GetWorld()->SetMode(std::move(mode));
						} else { // TC
							auto mode = stmp::make_unique<TCGameMode>(*GetWorld());

							int trNum = r.ReadByte();
							for (int i = 0; i < trNum; i++) {
								TCGameMode::Territory t{*mode};
								t.pos = r.ReadVector3();

								int state = r.ReadByte();
								t.ownerTeamId = state;
								t.progressBasePos = 0.0F;
								t.progressStartTime = 0.0F;
								t.progressRate = 0.0F;
								t.capturingTeamId = -1;
								mode->AddTerritory(t);
							}

							GetWorld()->SetMode(std::move(mode));
						}
						client->JoinedGame();

						if (client->Replaying)
							joinReplay();
					}
					break;
				case PacketTypeKillAction: {
					Player& p = GetPlayer(r.ReadByte());
					Player* killer = &GetPlayer(r.ReadByte());
					int kt = r.ReadByte();
					KillType type;
					switch (kt) {
						case 0: type = KillTypeWeapon; break;
						case 1: type = KillTypeHeadshot; break;
						case 2: type = KillTypeMelee; break;
						case 3: type = KillTypeGrenade; break;
						case 4: type = KillTypeFall; break;
						case 5: type = KillTypeTeamChange; break;
						case 6: type = KillTypeClassChange; break;
						default: SPInvalidEnum("kt", kt);
					}

					int respawnTime = r.ReadByte();
					switch (type) {
						case KillTypeFall:
						case KillTypeClassChange:
						case KillTypeTeamChange: killer = &p; break;
						default: break;
					}
					p.KilledBy(type, *killer, respawnTime);
					if (&p != killer)
						GetWorld()->GetPlayerPersistent(killer->GetId()).kills++;
				} break;
				case PacketTypeChatMessage: {
					// might be wrong player id for server message
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					int type = r.ReadByte();
					std::string msg = r.ReadRemainingString();

					if (type == 2) {
						client->ServerSentMessage(false, msg);

						// Speculate the best game properties based on the server generated messages
						properties->HandleServerMessage(msg);
					} else if (type == 0 || type == 1) {
						if (p)
							client->PlayerSentChatMessage(*p, (type == 0), TrimSpaces(msg));
						else
							client->ServerSentMessage((type == 1), TrimSpaces(msg));
					}
				} break;
				case PacketTypeMapStart: {
					// next map!
					if (protocolVersion == 4) {
						// The AoS 0.76 protocol allows the client to load a map from a local cache
						// if possible. After receiving MapStart, the client should respond with
						// MapCached to indicate whether the map with a given checksum exists in the
						// cache or not. We didn't implement a local cache, so we always ask the
						// server to send fresh map data.
						NetPacketWriter w(PacketTypeMapCached);
						w.WriteByte((uint8_t)0);
						enet_peer_send(peer, 0, w.CreatePacket());
					}

					client->SetWorld(NULL);

					auto mapSize = r.ReadInt();
					SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

					mapLoader.reset(new GameMapLoader());
					mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

					status = NetClientStatusReceivingMap;
					statusString = _Tr("NetClient", "Loading snapshot");
				} break;
				case PacketTypeMapChunk: SPRaise("Unexpected: received Map Chunk while game");
				case PacketTypePlayerLeft: {
					Player& p = GetPlayer(r.ReadByte());

					client->PlayerLeaving(p);
					GetWorld()->GetPlayerPersistent(p.GetId()).kills = 0;

					savedPlayerTeam[p.GetId()] = -1;
					GetWorld()->SetPlayer(p.GetId(), NULL);
				} break;
				case PacketTypeTerritoryCapture: {
					int territoryId = r.ReadByte();
					bool winning = r.ReadByte() != 0;
					int state = r.ReadByte();

					// TODO: This piece is repeated for at least three times
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeTerritoryCapture"
						      "because game mode isn't specified yet");
						break;
					}

					if (mode->ModeType() != IGameMode::m_TC)
						SPRaise("Received PacketTypeTerritoryCapture in non-TC gamemode");

					TCGameMode& tc = dynamic_cast<TCGameMode&>(*mode);

					if (territoryId >= tc.GetNumTerritories()) {
						SPRaise("Invalid territory id %d specified (max = %d)",
							territoryId, tc.GetNumTerritories() - 1);
					}

					client->TeamCapturedTerritory(state, territoryId);

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.ownerTeamId = state;
					t.progressBasePos = 0.0F;
					t.progressRate = 0.0F;
					t.progressStartTime = 0.0F;
					t.capturingTeamId = -1;

					if (winning)
						client->TeamWon(state);
				} break;
				case PacketTypeProgressBar: {
					int territoryId = r.ReadByte();
					int capturingTeam = r.ReadByte();
					int rate = (int8_t)r.ReadByte();
					float progress = r.ReadFloat();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeProgressBar"
						      "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_TC)
						SPRaise("Received packet in non-TC gamemode");

					TCGameMode& tc = dynamic_cast<TCGameMode&>(*mode);

					if (territoryId >= tc.GetNumTerritories()) {
						SPRaise("Invalid territory id %d specified (max = %d)",
							territoryId, tc.GetNumTerritories() - 1);
					}

					if (progress < -0.1F || progress > 1.1F)
						SPRaise("Progress value out of range(%f)", progress);

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.progressBasePos = progress;
					t.progressRate = (float)rate * TC_CAPTURE_RATE;
					t.progressStartTime = GetWorld()->GetTime();
					t.capturingTeamId = capturingTeam;
				} break;
				case PacketTypeIntelCapture: {
					if (!GetWorld())
						SPRaise("No world");

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelCapture"
						      "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received PacketTypeIntelCapture in non-TC gamemode");
					CTFGameMode& ctf = dynamic_cast<CTFGameMode&>(mode.value());

					Player& p = GetPlayer(r.ReadByte());
					client->PlayerCapturedIntel(p);
					GetWorld()->GetPlayerPersistent(p.GetId()).kills += 10;
					ctf.GetTeam(p.GetTeamId()).hasIntel = false;
					ctf.GetTeam(p.GetTeamId()).score++;

					bool winning = r.ReadByte() != 0;
					if (winning) {
						ctf.ResetTeamScoreAndIntelHoldingStatus();
						client->TeamWon(p.GetTeamId());
					}
				} break;
				case PacketTypeIntelPickup: {
					Player& p = GetPlayer(r.ReadByte());
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelPickup"
						      "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received packet in non-TC gamemode");

					CTFGameMode& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(p.GetTeamId());
					team.hasIntel = true;
					team.carrierId = p.GetId();
					client->PlayerPickedIntel(p);
				} break;
				case PacketTypeIntelDrop: {
					Player& p = GetPlayer(r.ReadByte());
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelDrop"
						      "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received packet in non-TC gamemode");

					CTFGameMode& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(p.GetTeamId());
					team.hasIntel = false;

					ctf.GetTeam(1 - p.GetTeamId()).flagPos = r.ReadVector3();
					client->PlayerDropIntel(p);
				} break;
				case PacketTypeRestock: {
					int pId = r.ReadByte(); // skip player id
					Player& p = GetLocalPlayer();
					p.Restock();
				} break;
				case PacketTypeFogColour: {
					if (GetWorld()) {
						int a = r.ReadByte(); // skip alpha value
						GetWorld()->SetFogColor(r.ReadIntColor());
					}
				} break;
				case PacketTypeWeaponReload: {
					Player& p = GetPlayer(r.ReadByte());
					if (&p != GetLocalPlayerOrNull()) {
						p.Reload();
					} else {
						int clip = r.ReadByte();
						int reserve = r.ReadByte();
						p.ReloadDone(clip, reserve);
					}
				} break;
				case PacketTypeChangeTeam: {
					Player& p = GetPlayer(r.ReadByte());
					int team = r.ReadByte();
					if (team < 0 || team > 2)
						SPRaise("Received invalid team: %d", team);
					p.SetTeam(team);
				} break;
				case PacketTypeChangeWeapon: {
					//Player& p = GetPlayer(r.ReadByte());
					//lets not do this since main openspades doesnt do it either. caused a crash once for someone. 
					int weapon = r.ReadByte();

					WeaponType wType;
					switch (weapon) {
						case 0: wType = RIFLE_WEAPON; break;
						case 1: wType = SMG_WEAPON; break;
						case 2: wType = SHOTGUN_WEAPON; break;
						default: SPRaise("Received invalid weapon: %d", weapon);
					}
					// maybe this command is intended to change local player's
					// weapon...
					// p->SetWeaponType(wType);
				} break;
				default:
					printf("WARNING: dropped packet %d\n", (int)r.GetType());
					r.DumpDebug();
			}
		}

		void NetClient::SendVersionEnhanced(const std::set<std::uint8_t>& propertyIds) {
			if (client->Replaying)
				return;

			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)'x');

			for (std::uint8_t propertyId : propertyIds) {
				w.WriteByte(propertyId);

				auto lengthLabel = w.GetPosition();
				w.WriteByte((uint8_t)0); // dummy data for "Payload Length"

				auto beginLabel = w.GetPosition();
				switch (static_cast<VersionInfoPropertyId>(propertyId)) {
					case VersionInfoPropertyId::ApplicationNameAndVersion:
						w.WriteByte((uint8_t)OpenSpades_VERSION_MAJOR);
						w.WriteByte((uint8_t)OpenSpades_VERSION_MINOR);
						w.WriteByte((uint8_t)OpenSpades_VERSION_REVISION);
						w.WriteString("OpenSpades");
						break;
					case VersionInfoPropertyId::UserLocale:
						w.WriteString(GetCurrentLocaleAndRegion());
						break;
					case VersionInfoPropertyId::ClientFeatureFlags1: {
						auto flags = ClientFeatureFlags1::None;

						if (cg_unicode)
							flags |= ClientFeatureFlags1::SupportsUnicode;

						w.WriteInt(static_cast<uint32_t>(flags));
					} break;
					default:
						// Just return empty payload for an unrecognized property
						break;
				}

				w.Update(lengthLabel, (uint8_t)(w.GetPosition() - beginLabel));
				enet_peer_send(peer, 0, w.CreatePacket());
			}
		}

		void NetClient::SendJoin(int team, WeaponType weapType, std::string name, int kills) {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			int weapId;
			switch (weapType) {
				case RIFLE_WEAPON: weapId = 0; break;
				case SMG_WEAPON: weapId = 1; break;
				case SHOTGUN_WEAPON: weapId = 2; break;
				default: SPInvalidEnum("weapType", weapType);
			}

			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)0); // Player ID, but shouldn't matter here
			w.WriteByte((uint8_t)team);
			w.WriteByte((uint8_t)weapId);
			w.WriteByte((uint8_t)2); // TODO: change tool
			w.WriteInt((uint32_t)kills);
			w.WriteColor(GetWorld()->GetTeamColor(team));
			w.WriteString(name, 16);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendPosition(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypePositionData);
			w.WriteVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendOrientation(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeOrientationData);
			w.WriteVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendPlayerInput(PlayerInput inp) {
			SPADES_MARK_FUNCTION();

			uint8_t bits =
				inp.moveForward << 0 |
				inp.moveBackward << 1 |
				inp.moveLeft << 2 |
				inp.moveRight << 3 |
				inp.jump << 4 |
				inp.crouch << 5 |
				inp.sneak << 6 |
				inp.sprint << 7;

			if ((unsigned int)bits == lastPlayerInput)
				return;

			lastPlayerInput = bits;

			NetPacketWriter w(PacketTypeInputData);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte(bits);

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendWeaponInput(WeaponInput inp) {
			SPADES_MARK_FUNCTION();

			uint8_t bits = inp.primary << 0 | inp.secondary << 1;

			if ((unsigned int)bits == lastWeaponInput)
				return;

			lastWeaponInput = bits;

			NetPacketWriter w(PacketTypeWeaponInput);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte(bits);

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendBlockAction(spades::IntVector3 v, BlockActionType type) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeBlockAction);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			switch (type) {
				case BlockActionCreate: w.WriteByte((uint8_t)0); break;
				case BlockActionTool: w.WriteByte((uint8_t)1); break;
				case BlockActionDig: w.WriteByte((uint8_t)2); break;
				case BlockActionGrenade: w.WriteByte((uint8_t)3); break;
				default: SPInvalidEnum("type", type);
			}
			w.WriteIntVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendBlockLine(spades::IntVector3 v1, spades::IntVector3 v2) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeBlockLine);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteIntVector3(v1);
			w.WriteIntVector3(v2);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendReload() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeWeaponReload);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)0); // clip_ammo; not used?
			w.WriteByte((uint8_t)0); // reserve_ammo; not used?

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendHeldBlockColor() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeSetColour);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteColor(GetLocalPlayer().GetBlockColor());

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendTool() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeSetTool);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			switch (GetLocalPlayer().GetTool()) {
				case Player::ToolSpade: w.WriteByte((uint8_t)0); break;
				case Player::ToolBlock: w.WriteByte((uint8_t)1); break;
				case Player::ToolWeapon: w.WriteByte((uint8_t)2); break;
				case Player::ToolGrenade: w.WriteByte((uint8_t)3); break;
				default: SPInvalidEnum("tool", GetLocalPlayer().GetTool());
			}

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendGrenade(const Grenade& g) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeGrenadePacket);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteFloat(g.GetFuse());
			w.WriteVector3(g.GetPosition());
			w.WriteVector3(g.GetVelocity());

			ENetPacket *pkt = w.CreatePacket();
			enet_peer_send(peer, 0, pkt);
			RegisterDemoPacket(pkt);
		}

		void NetClient::SendHit(int targetPlayerId, HitType type) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeHitPacket);
			w.WriteByte((uint8_t)targetPlayerId);
			switch (type) {
				case HitTypeTorso: w.WriteByte((uint8_t)0); break;
				case HitTypeHead: w.WriteByte((uint8_t)1); break;
				case HitTypeArms: w.WriteByte((uint8_t)2); break;
				case HitTypeLegs: w.WriteByte((uint8_t)3); break;
				case HitTypeMelee: w.WriteByte((uint8_t)4); break;
				default: SPInvalidEnum("type", type);
			}
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendChat(std::string text, bool global) {
			if (client->Replaying) {
				DemoCommands(text);
				return;
			}

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChatMessage);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)(global ? 0 : 1));
			w.WriteString(text);
			w.WriteByte((uint8_t)0);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendWeaponChange(WeaponType wType) {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChangeWeapon);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)wType);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendTeamChange(int team) {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChangeTeam);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)team);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendHandShakeValid(int challenge) {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeHandShakeReturn);
			w.WriteInt((uint32_t)challenge);

			SPLog("Sending hand shake back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendVersion() {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeVersionSend);
			w.WriteByte((uint8_t)'o');
			w.WriteByte((uint8_t)OpenSpades_VERSION_MAJOR);
			w.WriteByte((uint8_t)OpenSpades_VERSION_MINOR);
			w.WriteByte((uint8_t)OpenSpades_VERSION_REVISION);
			w.WriteString(VersionInfo::GetVersionInfo());

			SPLog("Sending version back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendSupportedExtensions() {
			if (client->Replaying)
				return;

			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeExtensionInfo);
			w.WriteByte(static_cast<uint8_t>(extensions.size()));
			for (const auto& i : extensions) {
				w.WriteByte(static_cast<uint8_t>(i.first));  // ext id
				w.WriteByte(static_cast<uint8_t>(i.second)); // ext version
			}

			SPLog("Sending extension support.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::MapLoaded() {
			SPADES_MARK_FUNCTION();

			SPAssert(mapLoader);

			// Move `mapLoader` to a local variable so that the associated resources
			// are released as soon as possible when no longer needed
			std::unique_ptr<GameMapLoader> mapLoader = std::move(this->mapLoader);
			mapLoadMonitor.reset();

			SPLog("Waiting for the game map decoding to complete...");
			mapLoader->MarkEOF();
			mapLoader->WaitComplete();
			GameMap* map = mapLoader->TakeGameMap().Unmanage();
			SPLog("The game map was decoded successfully.");

			// now initialize world
			World* w = new World(properties);
			w->SetMap(map);
			map->Release();
			SPLog("World initialized.");

			client->SetWorld(w);

			SPAssert(GetWorld());

			SPLog("World loaded. Processing saved packets (%d)...", (int)savedPackets.size());

			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			// do saved packets
			try {
				for (const auto& packets : savedPackets) {
					NetPacketReader r(packets);
					HandleGamePacket(r);
				}
				savedPackets.clear();
				SPLog("Done.");
			} catch (...) {
				savedPackets.clear();
				throw;
			}
		}

		float NetClient::GetMapReceivingProgress() {
			SPAssert(status == NetClientStatusReceivingMap);

			return mapLoader->GetProgress();
		}

		std::string NetClient::GetStatusString() {
			if (status == NetClientStatusReceivingMap) {
				// Display extra information
				auto text = mapLoadMonitor->GetDisplayedText();
				if (!text.empty())
					return Format("{0} ({1})", statusString, text);
			}

			return statusString;
		}

		NetClient::BandwidthMonitor::BandwidthMonitor(ENetHost* host)
		    : host(host), lastDown(0.0), lastUp(0.0) {
			sw.Reset();
		}

		void NetClient::BandwidthMonitor::Update() {
			if (sw.GetTime() > 0.5) {
				lastUp = host->totalSentData / sw.GetTime();
				lastDown = host->totalReceivedData / sw.GetTime();
				host->totalSentData = 0;
				host->totalReceivedData = 0;
				sw.Reset();
			}
		}

		NetClient::MapDownloadMonitor::MapDownloadMonitor(GameMapLoader& mapLoader)
		    : numBytesDownloaded{0}, mapLoader{mapLoader}, receivedFirstByte{false} {}

		void NetClient::MapDownloadMonitor::AccumulateBytes(unsigned int numBytes) {
			// It might take a while before receiving the first byte. Take this into account to
			// get a more accurate estimate of download time.
			if (!receivedFirstByte) {
				sw.Reset();
				receivedFirstByte = true;
			}

			numBytesDownloaded += numBytes;
		}

		std::string NetClient::MapDownloadMonitor::GetDisplayedText() {
			if (!receivedFirstByte)
				return {};

			float secsElapsed = static_cast<float>(sw.GetTime());
			if (secsElapsed <= 0.0F)
				return {};

			float progress = mapLoader.GetProgress();
			float bytesPerSec = static_cast<float>(numBytesDownloaded) / secsElapsed;
			float progressPerSec = progress / secsElapsed;

			std::string text = Format("{0} KB, {1} KB/s",
				(numBytesDownloaded + 500) / 1000, ((int)bytesPerSec + 500) / 1000);

			// Estimate the remaining time
			float secsLeft = (1.0F - progress) / progressPerSec;
			if (secsLeft < 86400.0F) {
				int secs = (int)secsLeft + 1;

				text += ", ";

				if (secs < 120)
					text += _Tr("NetClient", "{0}s left", secs);
				else
					text += _Tr("NetClient", "{0}m{1}s left", secs / 60, secs);
			}

			return text;
		}

		FILE* NetClient::HandleDemoFile(std::string file_name, bool replay) {
			FILE* file;
			if (!replay) {
				file = fopen(file_name.c_str(), "wb");

				// aos_replay version + 0.75 version
				unsigned char value = 1;
				fwrite(&value, sizeof(value), 1, file);

				value = 3;
				fwrite(&value, sizeof(value), 1, file);
			} else {
				file = fopen(file_name.c_str(), "rb");

				// aos_replay version + 0.75/0.76 version
				unsigned char value;
				fread(&value, sizeof(value), 1, file);
				if (value != 1) {
					SPLog("Unsupported aos_replay Demo version: %u", value);
					throw;
				}

				ProtocolVersion version;
				fread(&value, sizeof(value), 1, file);
				if (value != 3 && value != 4) {
					SPLog("Unsupported AoS protocol version: %u", value);
					throw;
				} else {
					protocolVersion = value;
					if (value == 3) {
						version = ProtocolVersion::v075;
					} else {
						version = ProtocolVersion::v076;
					}

				}
				float end_time;
				unsigned short len;
				while (fread(&end_time, sizeof(end_time), 1, file) == 1) {
					fread(&len, sizeof(len), 1, file);
					fseek(file, len, SEEK_CUR);
				}
				fseek(file, 2L, SEEK_SET);
				int hour = (int)end_time / 3600;
				int min  = ((int)end_time % 3600) / 60;
				int sec  = (int)end_time % 60;
				char buf[256];
				sprintf(buf, "%02d:%02d:%02d", hour, min, sec);
				demo_end_time = buf;

				savedPackets.clear();

				properties.reset(new GameProperties(version));

				status = NetClientStatusConnecting;
				statusString = _Tr("Demo Replay", "Reading demo file");
			}

			return file;
		}

		void NetClient::RegisterDemoPacket(ENetPacket *packet) {
			if (!CurrentDemo.fp)
				return;

			float c_time = client->GetTimeGlobal() - CurrentDemo.start_time;
			unsigned short len = packet->dataLength;

			fwrite(&c_time, sizeof(c_time), 1, CurrentDemo.fp);
			fwrite(&len, sizeof(len), 1, CurrentDemo.fp);
			fwrite(packet->data, packet->dataLength, 1, CurrentDemo.fp);
		}

		void NetClient::DemoStart(std::string file_name, bool replay) {
			try {
				CurrentDemo.fp = HandleDemoFile(file_name, replay);
			} catch (...) {
				return;
			}
			CurrentDemo.start_time = client->GetTimeGlobal();
			CurrentDemo.delta_time = 0.0f;
			DemoStarted = !replay;
			DemoSkippingMap = DemoPaused = PauseDemoAfterSkip = false;
			demo_skip_time = demo_count_ups = demo_next_ups = 0;
			DemoFirstJoined = true;
		}

		void NetClient::DemoStop() {
			DemoStarted = false;
			if (CurrentDemo.fp)
				fclose(CurrentDemo.fp);

			CurrentDemo = ResetStruct;
		}

		void NetClient::joinReplay() {
			SPADES_SETTING(cg_playerName);
			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)33); // Player ID, but shouldn't matter here
			w.WriteByte((uint8_t)255);
			w.WriteByte((uint8_t)0);
			w.WriteByte((uint8_t)2); // TODO: change tool
			w.WriteInt((uint32_t)0);
			w.WriteColor(GetWorld()->GetTeamColor(255));
			w.WriteString(cg_playerName, 16);
			NetPacketReader read(w.CreatePacket());

			HandleGamePacket(read);
			if (DemoSkippingMap && demo_skip_time == 0) {
				CurrentDemo.start_time = client->GetTimeGlobal() * client->DemoSpeedMultiplier - CurrentDemo.delta_time;
				DemoSkippingMap = false;
			} else if (PauseDemoAfterSkip) {
				DemoCommandPause();
			}
			DemoSetFollow();
		}

		void NetClient::DemoCommands(std::string command) {
			if (command == "pause") {
				if (!DemoPaused) {
					DemoCommandPause();
				} else {
					DemoCommandUnpause(true);
				}
				return;
			}
			if (command == "unpause" && DemoPaused) {
				DemoCommandUnpause(true);
				return;
			}

			if ((int)command.size() <= 3)
				return;

			if (command.find( "sp ", 0) == 0) {//speed. set replay speed. 
				command = command.substr(3, (int)command.size());
				for (size_t i = 0; i < command.size(); i++) {
					if (!isdigit(command[i]) && command[i] != '.') {
						return;
					}
				}
				DemoCommandSP(std::stof(command));
				return;
			}
			if (command.find( "gt ", 0 ) == 0) {//GoTo. set demo time. 
				DemoCommandGT(command);
				return;
			}

			int value = DemoStringToInt(command.substr(3, (int)command.size()));
			if (value == -1 || value == 0) 
				return;

			if (command.find( "nu ", 0 ) == 0 && DemoPaused) {//next update. advance to next amount of world updates on pause (ups, update per second)
				DemoCommandNextUps(value);
				return;
			}
			if (command.find( "pu ", 0 ) == 0 && DemoPaused) {//prev update. rewind to previous amount of world updates on pause (ups, update per second)
				DemoCommandPrevUps(value);
				return;
			}
			if (command.find( "ff ", 0 ) == 0) {//fastforward
				DemoCommandFF(value);
				return;
			}
			if (command.find( "bb ", 0 ) == 0) {//rewind. actually starts all over again and fastforwards to time where u want to rewind to. 
				DemoCommandBB(value);
				return;
			}
		}

		int NetClient::DemoStringToInt(std::string integer) {
			for (size_t i = 0; i < integer.size(); i++) {
				if (!isdigit(integer[i])) {
					return -1;
				}
			}
			return std::stoi(integer);
		}

		void NetClient::DemoCommandPause() {
			DemoPaused = true;
			PauseDemoAfterSkip = true;
		}

		void NetClient::DemoCommandUnpause(bool skipped) {
			CurrentDemo.start_time = client->GetTimeGlobal() * client->DemoSpeedMultiplier - CurrentDemo.delta_time;
			DemoPaused = false;
			if (skipped) { //need to temporarily unpause during fastforward or rewind. only release pause when directly commanded.
				PauseDemoAfterSkip = false;
			}
		}

		void NetClient::DemoCommandFF(int seconds) {
			if (seconds == 0 || seconds == -1) {
				return;
			}
			if (PauseDemoAfterSkip) {
				DemoCommandUnpause(false);
			}
			demo_skip_time = seconds;
			CurrentDemo.start_time -= demo_skip_time;
			demo_skip_end_time = CurrentDemo.start_time + CurrentDemo.delta_time + demo_skip_time;
			DemoFollowState.first = client->GetFollowedPlayerId();
			DemoFollowState.second = client->GetFollowMode();
		}

		void NetClient::DemoCommandBB(int seconds) {
			if (fseek(CurrentDemo.fp, 2L, SEEK_SET) == 0) {
				if (seconds == 0 || seconds == -1) {
					return;
				}
				if (PauseDemoAfterSkip) {
					DemoCommandUnpause(false);
				}
				demo_skip_time = seconds;
				if (CurrentDemo.delta_time - demo_skip_time < 0) {
					demo_skip_time = CurrentDemo.delta_time;
				}
				CurrentDemo.start_time += demo_skip_time;
				demo_skip_end_time = CurrentDemo.start_time + CurrentDemo.delta_time - demo_skip_time;
				CurrentDemo.delta_time = demo_count_ups = 0;
				DemoFollowState.first = client->GetFollowedPlayerId();
				DemoFollowState.second = client->GetFollowMode();
			}
		}

		void NetClient::DemoCommandGT(std::string delta) {
			delta = delta.substr(3, (int)delta.size());
			std::vector<int> timestamp;
			int previndex = 0;
			for (size_t i = 0; i < delta.size(); i++) {
				if ((int)timestamp.size() >= 3)
					break;

				if (delta[i] == ':') {
					timestamp.push_back(DemoStringToInt(delta.substr(previndex, i - previndex)));
					previndex = i + 1;
				} else if (!isdigit(delta[i])) {
					return;
				} else if (i == (int)delta.size() - 1) {
					timestamp.push_back(DemoStringToInt(delta.substr(previndex, i - previndex + 1)));
				}
			}
			previndex = (int)timestamp.size();
			for (int i = previndex; i > 0; i--) {
				//sec
				if (i == previndex) {
					if (previndex == 1 && timestamp[i - 1] > 60 * 60 * 10) //still allow pure second command. 10 hour cap here aswell. 
						return;
					if (previndex > 1 && timestamp[i - 1] > 59)
						return;
					demo_skip_time = timestamp[i - 1];
				}
				//min
				if (i == previndex - 1) {
					if (timestamp[i - 1] > 59)
						return;
					demo_skip_time += timestamp[i - 1] * 60;
				}
				//hour
				if (i == previndex - 2) {
					if (timestamp[i - 1] > 10) //10 hours is still an unreasonable length for a recording. this is very generous. 
						return;
					demo_skip_time += timestamp[i - 1] * 60 * 60;
				}
			}
			if (demo_skip_time > CurrentDemo.delta_time) {
				DemoCommandFF(demo_skip_time - (int)CurrentDemo.delta_time);
			}
			else if (demo_skip_time < CurrentDemo.delta_time) {
				DemoCommandBB((int)CurrentDemo.delta_time - demo_skip_time);
			}
		}

		void NetClient::DemoCommandSP(float speed) {
			if (speed > 10 || speed < 0.1f) {
				return;
			}
			client->DemoSpeedMultiplier = speed;
			CurrentDemo.start_time = client->GetTimeGlobal() * speed - CurrentDemo.delta_time;
		}

		void NetClient::DemoCommandNextUps(int ups) {
			if (ups == 0 || ups == -1) {
				return;
			}
			demo_skip_time = demo_next_ups = ups;
			DemoCommandUnpause(false);
			CurrentDemo.start_time -= demo_next_ups * 10;
			demo_skip_end_time = CurrentDemo.start_time + CurrentDemo.delta_time;
			PrevUps = false;
			DemoFollowState.first = client->GetFollowedPlayerId();
			DemoFollowState.second = client->GetFollowMode();
		}

		void NetClient::DemoCommandPrevUps(int ups) {
			if (fseek(CurrentDemo.fp, 2L, SEEK_SET) == 0) {
				if (ups == 0 || ups == -1) {
					return;
				}
				demo_skip_time = demo_next_ups = demo_count_ups - ups;
				DemoCommandUnpause(false);
				demo_skip_end_time = CurrentDemo.start_time + CurrentDemo.delta_time;
				CurrentDemo.delta_time = demo_count_ups = 0;
				PrevUps = true;
				DemoFollowState.first = client->GetFollowedPlayerId();
				DemoFollowState.second = client->GetFollowMode();
			}
		}

		void NetClient::DemoCountUps() {
			demo_count_ups += 1;
			if (demo_next_ups != 0) {
				if (!PrevUps) {
					demo_next_ups -= 1;
					if (demo_next_ups <= 0) {
						DemoSetFollow();
						CurrentDemo.start_time = client->GetTimeGlobal() * client->DemoSpeedMultiplier - CurrentDemo.delta_time;
						DemoCommandPause();
					}
				} else {
					if (demo_count_ups >= demo_next_ups) {
						demo_next_ups = demo_skip_time = 0;
						DemoSetFollow();
						CurrentDemo.start_time = client->GetTimeGlobal() * client->DemoSpeedMultiplier - CurrentDemo.delta_time;
						DemoCommandPause();
					}
				}
			}
		}

		int NetClient::GetDemoTimer() {
			return CurrentDemo.delta_time;
		}

		void NetClient::DemoSkipMap() {
			if (!DemoSkippingMap && demo_skip_time == 0) {
				CurrentDemo.start_time -= 300; //maptransfer cant be longer than 5 minutes. this is more than generous.
				DemoSkippingMap = true;
			}
		}

		void NetClient::DemoSetFollow() {
			if (!GetWorld())
				return;
			if (!GetWorld()->GetPlayer(DemoFollowState.first))
				return;

			stmp::optional<Player&> p = GetWorld()->GetPlayer(DemoFollowState.first);
			if (p->IsSpectator())
				return;
			if (p->GetFront().GetSquaredLength() < 0.01F)
				return;

			client->SetFollowedPlayerId(DemoFollowState.first);
			client->SetFollowMode(DemoFollowState.second);
		}

		void NetClient::ReadNextDemoPacket() {
			if (!CurrentDemo.fp)
				return;

			float c_time;
			unsigned short len;

			if (fread(&c_time, sizeof(c_time), 1, CurrentDemo.fp) != 1) {
				if (GetWorld()) {
					client->SetWorld(NULL);
				}
				status = NetClientStatusNotConnected;
				if (feof(CurrentDemo.fp)) {
					statusString = "Demo Ended: End of Recording reached";
					SPRaise("Demo Ended: End of Recording reached");
				} else {
					statusString = "Demo Ended: Error";
					SPRaise("Demo Ended: Error");
				}
				throw;
			}
			CurrentDemo.delta_time = c_time;

			fread(&len, sizeof(len), 1, CurrentDemo.fp);
			CurrentDemo.data.resize(len);

			fread(CurrentDemo.data.data(), len, 1, CurrentDemo.fp);
		}

		void NetClient::DoDemo() {
			if (status == NetClientStatusNotConnected)
				return;

			if (DemoPaused && demo_skip_time == 0)
				return;

			if (demo_skip_time != 0 && CurrentDemo.start_time + CurrentDemo.delta_time >= demo_skip_end_time) {
				demo_skip_time = 0;
				if (status == NetClientStatusReceivingMap) {
					DemoSkipMap();
				} else if (PauseDemoAfterSkip) {
					DemoCommandPause();
				}
				DemoSetFollow();
			}
			
			while (CurrentDemo.start_time + CurrentDemo.delta_time < client->GetTimeGlobal() * client->DemoSpeedMultiplier) {
				try {
					ReadNextDemoPacket();
				} catch (...) {
					throw;
				}
				NetPacketReader reader(CurrentDemo.data);

				if (demo_skip_time != 0) {
					if (reader.GetType() == PacketTypeGrenadePacket) {
						continue; //after skipping, all nades from during the skip would spawn and explode simultaneously. so ignore nades during skips. 
					}
				}
				//ideally instead of repeating event handler here, maybe break the following part into a third function that would be used by both demo and event handler. 
				if (status == NetClientStatusConnecting) {
					reader.DumpDebug();
					if (reader.GetType() != PacketTypeMapStart)
						SPRaise("Unexpected packet: %d", (int)reader.GetType());

					auto mapSize = reader.ReadInt();
					SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

					mapLoader.reset(new GameMapLoader());
					mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

					status = NetClientStatusReceivingMap;
					statusString = _Tr("Demo Replay", "Loading snapshot");
					DemoSkipMap();
				} else if (status == NetClientStatusReceivingMap) {
					SPAssert(mapLoader);
					if (reader.GetType() == PacketTypeMapChunk) {
						std::vector<char> dt = reader.GetData();

						mapLoader->AddRawChunk(dt.data() + 1, dt.size() - 1);
						mapLoadMonitor->AccumulateBytes(
						  static_cast<unsigned int>(dt.size() - 1));
					} else {
						reader.DumpDebug();
						if (reader.GetType() == PacketTypeStateData) {
							status = NetClientStatusConnected;
							statusString = _Tr("NetClient", "Connected");

							try {
								MapLoaded();
							} catch (const std::exception& ex) {
								if (strstr(ex.what(), "File truncated") ||
								    strstr(ex.what(), "EOF reached")) {
									SPLog("Map decoder returned error:\n%s", ex.what());
									DemoStop();
									statusString = _Tr("Demo Replay", "Error");
									throw;
								}
							} catch (...) {
								DemoStop();
								statusString = _Tr("Demo Replay", "Error");
								throw;
							}

							HandleGamePacket(reader);
						} else if (reader.GetType() == PacketTypeWeaponReload) {
							// Drop the reload packet. Pyspades does not
							// cancel the reload packets on map change and
							// they would cause an error if we would
							// process them
						} else {
							// Save the packet for later
							savedPackets.push_back(reader.GetData());
						}
					}
				} else if (status == NetClientStatusConnected) {
					try {
						HandleGamePacket(reader);
						if (reader.GetType() == PacketTypeMapStart) {
							DemoSkipMap();
						}
					} catch (const std::exception& ex) {
						int type = reader.GetType();
						reader.DumpDebug();
						SPRaise("Exception while handling packet type 0x%08x:\n%s", type, ex.what());
					}
				}
			}
		}
	} // namespace client
} // namespace spades
