{%- for pkt in parser.recv_pkt %}
bool Handle_{{pkt.name}}(PacketSessionRef& session, Protocol::{{pkt.name}}& pkt)
{
	// TODO: 여기에 패킷 처리 로직을 작성하세요.
	return true;
}
{%- endfor %}
