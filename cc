import discord
from discord.ext import commands, tasks
import random
import sqlite3
import time
import datetime
import asyncio
from discord import File
import os
from dotenv import load_dotenv
import shop
from constants import RANDOM_BOX_COSTS
from constants import WOOD_BOX_REWARDS
from constants import GOLD_TO_TOKENS
from discord.ext.menus import Menu, button



load_dotenv('F:/python/overcoinraffle/token.env')

TOKEN = os.getenv('TOKEN')
intents = discord.Intents.default()
intents.message_content = True
intents.typing = False
intents.presences = False
bot = commands.Bot(command_prefix='!', intents=intents)

def connect_db():
    conn = sqlite3.connect("overcoin.db")
    conn.row_factory = sqlite3.Row
    conn.isolation_level = None  # เพิ่มบรรทัดนี้
    return conn

def setup_db():
    conn = connect_db()
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT, tokens INTEGER, join_time TEXT, spam_count INTEGER)''')
    c.execute('''CREATE TABLE IF NOT EXISTS blocks (block_number TEXT, winning_number INTEGER, winner_id INTEGER, winner_username TEXT, timestamp REAL, time_elapsed REAL)''')
    c.execute('''CREATE TABLE IF NOT EXISTS exchanges (id INTEGER PRIMARY KEY, user_id INTEGER, username TEXT, resource TEXT, amount INTEGER, coins INTEGER, timestamp TEXT)''')

    conn.commit()
    conn.close()

target_number = random.randint(1, 10)
current_block = 1
start_time = time.time()
spam_limit = 5
spam_penalty_time = 60
async def set_player_role(member, role_name="Player"):
    role = discord.utils.get(member.guild.roles, name=role_name)
    if role is None:
        role = await member.guild.create_role(name=role_name, color=discord.Color.yellow())
    await member.add_roles(role)

async def delete_inactive_messages():
    channel_id = 1097916590223261817  # Replace with your channel ID
    channel = bot.get_channel(channel_id)
    async for message in channel.history():
        if (datetime.datetime.now(datetime.timezone.utc) - message.created_at).seconds > 600:
            await message.delete()


delete_inactive_messages_loop = tasks.loop(minutes=10)(delete_inactive_messages)

@bot.event
async def on_ready():
    print(f"{bot.user} พร้อมใช้งาน!")
    setup_db()
    await announce_target_number(target_number)
    delete_inactive_messages_loop.start()

@bot.command()
async def register(ctx):
    user_id = ctx.author.id
    username = str(ctx.author)
    conn = connect_db()
    c = conn.cursor()

    c.execute("INSERT OR IGNORE INTO users (id, username, tokens, join_time, spam_count) VALUES (?, ?, ?, ?, ?)",
              (user_id, username, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), 0))

    conn.commit()
    conn.close()

    await set_player_role(ctx.author)
    await ctx.send(f"ยินดีต้อนรับ {ctx.author.mention}! คุณได้สมัครเข้าร่วมเกมแล้ว")

@commands.has_role("Player")
@bot.command()
async def guess(ctx, number: int):
    global current_block, target_number, start_time, spam_limit, spam_penalty_time
    guessing_channel_id = 1097916590223261817
    admin_channel_id = 1098143249958436884  # ใส่ ID ของห้อง admin ที่นี่
    if ctx.channel.id != guessing_channel_id:
        return
    user_id = ctx.author.id
    conn = connect_db()
    c = conn.cursor()
    c.execute("SELECT join_time, spam_count FROM users WHERE id=?", (user_id,))
    result = c.fetchone()

    if not result:
        await ctx.send("คุณยังไม่ได้สมัครเข้าร่วมเกม กรุณาพิมพ์ !register เพื่อสมัคร")
        return

    join_time, spam_count = result
    join_time_datetime = datetime.datetime.strptime(join_time, "%Y-%m-%d %H:%M:%S")
    time_difference = datetime.datetime.now() - join_time_datetime
    wait_time = 5 * 60 + spam_count * spam_penalty_time

    if time_difference.seconds < wait_time:
        await ctx.send(f"คุณต้องรอ {wait_time // 60} นาทีหลังจากการทายครั้งล่าสุด")
        return

    if number == target_number:
        with connect_db() as conn:
            c = conn.cursor()
            c.execute("UPDATE users SET tokens = tokens + 5 WHERE id=?", (user_id,))
            conn.commit()
            elapsed_time = time.time() - start_time
            c.execute("INSERT INTO blocks (block_number, winning_number, winner_id, winner_username, timestamp, time_elapsed) VALUES (?, ?, ?, ?, ?, ?)",
                    (current_block, target_number, user_id, str(ctx.author), time.time(), elapsed_time))
            conn.commit()

        with open("winning_image.jpg", "rb") as image_file:
            winning_image = File(image_file, "winning_image.jpg")

        embed = discord.Embed(
            title="🎉 ยินดีด้วย! คุณทายถูก! 🎉",
            description=f"{ctx.author.mention} ได้รับ 5 Overcoin",
            color=discord.Color.green()
        )
        embed.set_image(url="attachment://winning_image.jpg")
        embed.set_thumbnail(url=ctx.author.avatar.url)
        await ctx.send(embed=embed, file=winning_image)

        target_number = random.randint(1, 10)
        current_block += 1
        start_time = time.time()
        await announce_target_number(target_number)

    else:
        await ctx.send("คุณทายผิด กรุณาลองอีกครั้ง")

    with connect_db() as conn:
        c = conn.cursor()
        c.execute("UPDATE users SET join_time = ?, spam_count = 0 WHERE id=?", (datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), user_id))
        conn.commit()

async def announce_target_number(target_number):
    admin_channel_id = 1098143249958436884  # ใส่ ID ของห้อง admin ที่นี่
    admin_channel = bot.get_channel(admin_channel_id)
    if admin_channel is not None:
        await admin_channel.send(f"ตัวเลขเป้าหมายใหม่: {target_number}")
    else:
        print("ไม่พบห้อง admin")

@commands.has_role("Player")
@bot.command()
async def token(ctx):
    user_id = ctx.author.id
    conn = connect_db()
    c = conn.cursor()
    c.execute("SELECT username, tokens, join_time FROM users WHERE id=?", (user_id,))
    result = c.fetchone()

    if not result:
        await ctx.send("คุณยังไม่ได้สมัครเข้าร่วมเกม กรุณาพิมพ์ !register เพื่อสมัคร")
        return

    username, tokens, join_time = result

    # ดึงข้อมูลทรัพยากรของผู้ใช้
    c.execute("SELECT resource, SUM(amount) as total_amount FROM exchanges WHERE user_id=? GROUP BY resource", (user_id,))
    resources = c.fetchall()

    embed = discord.Embed(
        title="🌟 ข้อมูล 🌟",
        description=f"👤 ชื่อ: {username}",
        color=discord.Color.purple()
    )
    embed.add_field(name="💰 Overcoin ที่มี", value=tokens, inline=False)
    embed.add_field(name="📅 วันเวลาที่สมัครเข้าร่วมเกม", value=join_time, inline=False)

    # แสดงรายการทรัพยากรของผู้ใช้
    if resources:
        menu = ResourceMenu(resources)
        await menu.start(ctx)
    else:
        embed.add_field(name="🚫 ทรัพยากรที่มี", value="คุณยังไม่มีทรัพยากรใด ๆ", inline=False)
        embed.set_thumbnail(url=ctx.author.avatar.url)
        await ctx.send(embed=embed)

    await asyncio.sleep(30)
    await ctx.message.delete()



@commands.has_role("Player")
@bot.command()
async def shop1(ctx):
    exchange_rates = shop.get_exchange_rates()
    embed = discord.Embed(title="ร้านค้า", description="อัตราการแลกทรัพยากร", color=discord.Color.blue())

    for resource, rate in exchange_rates.items():
        embed.add_field(name=f"1 Overcoin แลก {rate} {resource}", value="\u200b", inline=False)

    await ctx.send(embed=embed)

@commands.has_role("Player")
@bot.command(name="แลก")
async def exchange(ctx, resource: str, coins: int):
    exchange_rates = shop.get_exchange_rates()
    if resource in exchange_rates:
        rate = exchange_rates[resource]
        amount = rate * coins

        user_id = ctx.author.id
        conn = connect_db()
        c = conn.cursor()

        # ตรวจสอบยอด Overcoin ปัจจุบันของผู้ใช้
        c.execute("SELECT tokens FROM users WHERE id=?", (user_id,))
        result = c.fetchone()
        current_tokens = result['tokens']

        # ตรวจสอบว่ายอด Overcoin ปัจจุบันเพียงพอในการแลกหรือไม่
        if coins <= current_tokens:
            # หัก Overcoin ออกจากผู้ใช้
            c.execute("UPDATE users SET tokens = tokens - ? WHERE id=?", (coins, user_id))
            conn.commit()

            # บันทึกข้อมูลการแลกลงในฐานข้อมูล
            timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            c.execute("INSERT INTO exchanges (user_id, username, resource, coins, amount, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
                    (ctx.author.id, str(ctx.author), resource, coins, amount, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
            conn.commit()

            await ctx.send(f"คุณได้แลก {coins} Overcoin เพื่อรับ {amount} {resource}")
            # โค้ดสำหรับปรับปรุงตัวแปรและฐานข้อมูลตามที่คุณต้องการ
        else:
            await ctx.send("ขออภัย ยอด Overcoin ของคุณไม่เพียงพอในการแลก")

        conn.close()

    else:
        await ctx.send("ขออภัย ไม่พบทรัพยากรที่คุณต้องการแลก โปรดลองอีกครั้ง")

@commands.has_role("Player")
@bot.command()
async def โอน(ctx, resource: str, amount: int, receiver: discord.Member):
    sender_id = ctx.author.id
    sender_username = str(ctx.author)
    receiver_id = receiver.id
    receiver_username = str(receiver)

    if sender_id == receiver_id:
        await ctx.send("คุณไม่สามารถโอนทรัพยากรให้กับตัวเองได้")
        return

    conn = connect_db()
    c = conn.cursor()

    # ตรวจสอบว่าผู้ใช้มีทรัพยากรที่เพียงพอในการโอนหรือไม่
    c.execute("SELECT SUM(amount) as total_amount FROM exchanges WHERE user_id=? AND resource=?", (sender_id, resource))
    result = c.fetchone()
    current_amount = result['total_amount']
    if not current_amount or current_amount < amount:
        await ctx.send(f"ขออภัย คุณมี {current_amount or 0} {resource} เท่านั้น ไม่เพียงพอที่จะโอน {amount} {resource}")
        return

    # บันทึกข้อมูลการโอนลงในฐานข้อมูล
    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (sender_id, sender_username, resource, -amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (receiver_id, receiver_username, resource, amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    conn.commit()

    # สร้าง embed แสดงผลการโอน
    embed = discord.Embed(title="🔄 การโอนทรัพยากร", color=discord.Color.green())
    embed.set_author(name=ctx.author.display_name, icon_url=ctx.author.avatar.url)
    embed.add_field(name="ผู้โอน", value=ctx.author.mention, inline=False)
    embed.add_field(name="ผู้รับ", value=receiver.mention, inline=False)
    embed.add_field(name="ทรัพยากร", value=resource, inline=False)
    embed.add_field(name="จำนวน", value=f"{amount} {resource}", inline=False)
    embed.set_footer(text=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

    await ctx.send(embed=embed)

    await asyncio.sleep(30)
    await ctx.message.delete()


@commands.has_role("Player")
@bot.command()
async def แลกกล่อง(ctx, box_type: str, amount: int = 1):
    if box_type not in RANDOM_BOX_COSTS:
        await ctx.send("ประเภทของกล่องสุ่มไม่ถูกต้อง กรุณาเลือก ไม้, หิน หรือ เหล็ก")
        return

    if amount < 1:
        await ctx.send("จำนวนกล่องที่ต้องการแลกต้องมากกว่า 0")
        return

    user_id = ctx.author.id
    username = str(ctx.author)

    conn = connect_db()
    c = conn.cursor()

    for resource, cost in RANDOM_BOX_COSTS[box_type].items():
        c.execute("SELECT SUM(amount) as total_amount FROM exchanges WHERE user_id=? AND resource=?", (user_id, resource))
        result = c.fetchone()
        current_amount = result['total_amount']

        if not current_amount or current_amount < cost * amount:
            await ctx.send(f"ขออภัย คุณมี {current_amount or 0} {resource} เท่านั้น ไม่เพียงพอที่จะแลกกล่อง {box_type} {amount} กล่อง")
            return

        c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
                  (user_id, username, resource, -cost * amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))

    box_item_name = f"กล่อง{box_type}"
    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (user_id, username, box_item_name, amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))

    conn.commit()

    embed = discord.Embed(title="🔄 การแลกกล่องสุ่ม", color=discord.Color.green())
    embed.set_author(name=ctx.author.display_name, icon_url=ctx.author.avatar.url)
    embed.add_field(name="ผู้ใช้", value=ctx.author.mention, inline=False)
    embed.add_field(name="ประเภทของกล่อง", value=box_type, inline=False)
    embed.add_field(name="จำนวนกล่อง", value=amount, inline=False)
    embed.set_footer(text=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

    await ctx.send(embed=embed)


@commands.has_role("Player")
@bot.command()
async def สุ่มกล่องไม้(ctx):
    user_id = ctx.author.id
    username = str(ctx.author)

    conn = connect_db()
    c = conn.cursor()

    c.execute("SELECT SUM(amount) as total_amount FROM exchanges WHERE user_id=? AND resource=?", (user_id, "กล่องไม้"))
    result = c.fetchone()
    current_amount = result['total_amount']

    if not current_amount or current_amount < 1:
        await ctx.send("ขออภัย คุณไม่มีกล่องไม้เพียงพอสำหรับการสุ่ม")
        return

    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (user_id, username, "กล่องไม้", -1, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    conn.commit()

    countdown_embed = discord.Embed(title="🔄 กำลังสุ่มกล่องไม้", color=discord.Color.green())
    countdown_embed.set_author(name=ctx.author.display_name, icon_url=ctx.author.avatar.url)
    countdown_embed.set_footer(text="กรุณารอสักครู่...")
    countdown_message = await ctx.send(embed=countdown_embed)

    await asyncio.sleep(3)
    await countdown_message.delete()

    reward_name, reward_amount = random.choice(list(WOOD_BOX_REWARDS.items()))

    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (user_id, username, reward_name, reward_amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    conn.commit()

    reward_embed = discord.Embed(title="🎁 ผลการสุ่มกล่องไม้", color=discord.Color.green())
    reward_embed.set_author(name=ctx.author.display_name, icon_url=ctx.author.avatar.url)
    reward_embed.add_field(name="ผู้ใช้", value=ctx.author.mention, inline=False)
    reward_embed.add_field(name="ได้รับ", value=f"{reward_name}: {reward_amount}", inline=False)
    reward_embed.set_footer(text=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

    await ctx.send(embed=reward_embed)

@commands.has_role("Player")
@bot.command()
async def ขายทอง(ctx, amount: int):
    user_id = ctx.author.id
    username = str(ctx.author)

    conn = connect_db()
    c = conn.cursor()

    c.execute("SELECT SUM(amount) as total_amount FROM exchanges WHERE user_id=? AND resource=?", (user_id, "ทอง"))
    result = c.fetchone()
    current_amount = result['total_amount']

    if not current_amount or current_amount < amount:
        await ctx.send(f"ขออภัย คุณมี {current_amount or 0} ทอง เท่านั้น ไม่เพียงพอที่จะขาย {amount} ทอง")
        return

    tokens_to_receive = amount * GOLD_TO_TOKENS
    c.execute("UPDATE users SET tokens = tokens + ? WHERE id=?", (tokens_to_receive, user_id))
    c.execute("INSERT INTO exchanges (user_id, username, resource, amount, coins, timestamp) VALUES (?, ?, ?, ?, ?, ?)",
              (user_id, username, "ทอง", -amount, 0, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    conn.commit()

    embed = discord.Embed(title="💰 การขายทอง", color=discord.Color.gold())
    embed.set_author(name=ctx.author.display_name, icon_url=ctx.author.avatar.url)
    embed.add_field(name="ขาย", value=f"{amount} ทอง", inline=True)
    embed.add_field(name="ได้รับ", value=f"{tokens_to_receive} Overcoin", inline=True)
    embed.set_footer(text=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

    await ctx.send(embed=embed)

class ResourceMenu(Menu):
    def __init__(self, resources):
        super().__init__()
        self.resources = resources
        self.current_page = 0

    async def send_initial_message(self, ctx, channel):
        embed = self.generate_embed(0)
        return await channel.send(embed=embed)

    def generate_embed(self, start_index):
        embed = discord.Embed(
            title="🌟 ข้อมูล 🌟",
            description=f"👤 ชื่อ: {self.ctx.author}",
            color=discord.Color.purple()
        )

        for resource, total_amount in self.resources[start_index:start_index+10]:
            embed.add_field(name=f"🌐 {resource}", value=total_amount, inline=True)

        embed.set_thumbnail(url=self.ctx.author.avatar.url)

        return embed

    @button('\u23ea')  # Button: "⏪"
    async def previous_page(self, payload):
        if self.current_page > 0:
            self.current_page -= 1
            await self.message.edit(embed=self.generate_embed(self.current_page * 10))

    @button('\u23e9')  # Button: "⏩"
    async def next_page(self, payload):
        if (self.current_page + 1) * 10 < len(self.resources):
            self.current_page += 1
            await self.message.edit(embed=self.generate_embed(self.current_page * 10))


bot.run(TOKEN)
