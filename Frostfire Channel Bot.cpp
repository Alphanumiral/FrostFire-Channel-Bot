#include "MyBot.h"
#include <dpp/dpp.h>

const std::string    BOT_TOKEN    = INSERT_TOKEN_HERE;

int main()
{
	//create bot cluster
	dpp::cluster bot(BOT_TOKEN);

	//storage of factory channel IDs and the IDs of channels created from the factory
	std::map<dpp::snowflake, std::vector<dpp::snowflake>> factories;

	//output simple log messages to stdout
	bot.on_log(dpp::utility::cout_logger());

	//creating slash commands so they can be used
	bot.on_ready([&bot](const dpp::ready_t& event) {
		//wrap command registration in run_once to make sure it doesnt run on every full reconnection
		if (dpp::run_once<struct register_bot_commands>()) {
			dpp::slashcommand factoryCommand("factory", "Create a factory channel", bot.me.id);
			factoryCommand.add_option(dpp::command_option(dpp::co_string, "name", "The name of the factory and resulting channels", true));

			dpp::slashcommand removeCommand("remove", "Remove a factory channel", bot.me.id);
			removeCommand.add_option(dpp::command_option(dpp::co_string, "name", "The name of the factory to be removed, case sensitive (will also remove child channels)", true));

			bot.global_command_create(factoryCommand);
			bot.global_command_create(removeCommand);
		}
	});

	//slash command handler
	bot.on_slashcommand([&bot, &factories](const dpp::slashcommand_t& event) {

		dpp::command_interaction cmd_data = event.command.get_command_interaction();
		dpp::guild server = event.command.get_guild();

		//creates a new voice channel with the specified name, entry is added to the factories map for ease of naming child channels and later removal
		if (event.command.get_command_name() == "factory") {
			std::string name = std::get<std::string>(event.get_parameter("name"));
			dpp::channel newChannel;
			newChannel.set_name(name);
			newChannel.set_type(dpp::CHANNEL_VOICE);
			newChannel.set_guild_id(server.id);
			bot.channel_create(newChannel, [&event, &factories](const dpp::confirmation_callback_t& callback) {
				dpp::channel createdChannel = std::get<dpp::channel>(callback.value);
				factories[createdChannel.id] = {};
			});
			
		}
		
		if (event.command.get_command_name() == "remove") {
			std::string name = std::get<std::string>(event.get_parameter("name"));
			bot.channels_get(event.command.guild_id, [&event, &name, &factories, &bot](const dpp::confirmation_callback_t& callback) {
				dpp::channel_map channels = std::get<dpp::channel_map>(callback.value);

				//search for the channel by name, once factory is found can iterate through vector of subchannels to delete all
				for (const auto& channel : channels) {
					if (channel.second.name.find(name) == 0 && factories.find(channel.second.id) != factories.end()) {
						dpp::snowflake factoryChannelID = channel.second.id;
						for (const dpp::snowflake child : factories[factoryChannelID]) {
							bot.channel_delete(child);
						}
						bot.channel_delete(factoryChannelID);
						factories.erase(factoryChannelID);
						break;
					}
				}

			});
		}
	});

	bot.on_voice_state_update([&bot, &factories](const dpp::voice_state_update_t& event) {
		//trimming raw event data string to relevant info. the channel id is used to find which factory the user entered. the user id and guild id are needed for moving the user to the created channel later
		int channelIDstart = event.raw_event.find("\"channel_id\"") + 13;
		int channelIDend = event.raw_event.find("\"", channelIDstart + 1);
		dpp::snowflake channelID = event.raw_event.substr(channelIDstart + 1, channelIDend - channelIDstart - 1);

		int userIDstart = event.raw_event.find("\"id\"") + 6;
		int userIDend = event.raw_event.find("\"", userIDstart + 1);
		dpp::snowflake userID = event.raw_event.substr(userIDstart, userIDend - userIDstart);

		int guildIDstart = event.raw_event.find("\"guild_id\"") + 12;
		int guildIDend = event.raw_event.find("\"", guildIDstart + 1);
		dpp::snowflake guildID = event.raw_event.substr(guildIDstart, guildIDend - guildIDstart);

		//iterate through factories until we find the one we're in
		for (auto const& factory : factories) {
			if (factory.first == channelID) {
				dpp::channel newChannel;
				newChannel.set_type(dpp::CHANNEL_VOICE);
				newChannel.set_guild_id(guildID);

				//getting the channel name of the factory, need to use futures as channel_get function is async
				std::promise<std::string> channelName;
				bot.channel_get(factory.first, [&newChannel, &factory, &channelName](const dpp::confirmation_callback_t& callback) {
					dpp::channel factoryChannel = std::get<dpp::channel>(callback.value);
					channelName.set_value(factoryChannel.name + " " + std::to_string(factory.second.size() + 1));
				});
				std::future<std::string> futureChannelName = channelName.get_future();
				futureChannelName.wait();
				newChannel.set_name(futureChannelName.get());

				//similar method here but ensuring the channel is created before co_guild_member_move is executed
				std::promise<dpp::snowflake> newChannelID;
				bot.channel_create(newChannel, [&factories, &factory, &newChannelID](const dpp::confirmation_callback_t& callback) {
					dpp::channel createdChannel = std::get<dpp::channel>(callback.value);
					factories[factory.first].push_back(createdChannel.id);
					newChannelID.set_value(createdChannel.id);
				});
				std::future<dpp::snowflake> futureChannelID = newChannelID.get_future();
				futureChannelID.wait();
				//we want to keep the factory clear so we immediately move the person to the channel they just created
				bot.co_guild_member_move(futureChannelID.get(), guildID, userID);

				break;
			}
		}
	});

	/* Start the bot */
	bot.start(dpp::st_wait);

	return 0;
}
